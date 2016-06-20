/*******************************************************************************
 *
 * Thunderbolt(TM) driver
 * Copyright(c) 2014 - 2017 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/prefetch.h>
#include <linux/highmem.h>
#include <linux/if_vlan.h>
#include <linux/jhash.h>
#include <linux/vmalloc.h>
#include <net/ip6_checksum.h>
#include "icm_nhi.h"
#include "net.h"

#define DEFAULT_MSG_ENABLE (NETIF_MSG_PROBE | NETIF_MSG_LINK | NETIF_MSG_IFUP)
static int debug = -1;
module_param(debug, int, 0000);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

#define TBT_NET_RX_HDR_SIZE 256

#define NUM_TX_LOGIN_RETRIES 60

#define APPLE_THUNDERBOLT_IP_PROTOCOL_REVISION 1

#define LOGIN_TX_PATH 0xf

#define TBT_NET_MTU (64 * 1024)

/* Number of Rx buffers we bundle into one write to the hardware */
#define TBT_NET_RX_BUFFER_WRITE	16

#define TBT_NET_MULTICAST_HASH_TABLE_SIZE 1024
#define TBT_NET_ETHER_ADDR_HASH(addr) (((addr[4] >> 4) | (addr[5] << 4)) % \
				       TBT_NET_MULTICAST_HASH_TABLE_SIZE)

#define BITS_PER_U32 (sizeof(u32) * BITS_PER_BYTE)

#define TBT_NET_NUM_TX_BUFS 256
#define TBT_NET_NUM_RX_BUFS 256
#define TBT_NET_SIZE_TOTAL_DESCS ((TBT_NET_NUM_TX_BUFS + TBT_NET_NUM_RX_BUFS) \
				  * sizeof(struct tbt_buf_desc))


#define TBT_NUM_FRAMES_PER_PAGE (PAGE_SIZE / TBT_RING_MAX_FRAME_SIZE)

#define TBT_NUM_BUFS_BETWEEN(idx1, idx2, num_bufs) \
	(((num_bufs) - 1) - \
	 ((((idx1) - (idx2)) + (num_bufs)) & ((num_bufs) - 1)))

#define TX_WAKE_THRESHOLD (2 * DIV_ROUND_UP(TBT_NET_MTU, \
			   TBT_RING_MAX_FRM_DATA_SZ))

#define TBT_NET_DESC_ATTR_SOF_EOF (((PDF_TBT_NET_START_OF_FRAME << \
				     DESC_ATTR_SOF_SHIFT) & \
				    DESC_ATTR_SOF_MASK) | \
				   ((PDF_TBT_NET_END_OF_FRAME << \
				     DESC_ATTR_EOF_SHIFT) & \
				    DESC_ATTR_EOF_MASK))

/* E2E workaround */
#define TBT_EXIST_BUT_UNUSED_HOPID 2

enum tbt_net_frame_pdf {
	PDF_TBT_NET_MIDDLE_FRAME,
	PDF_TBT_NET_START_OF_FRAME,
	PDF_TBT_NET_END_OF_FRAME,
};

struct thunderbolt_ip_login {
	struct thunderbolt_ip_header header;
	__be32 protocol_revision;
	__be32 transmit_path;
	__be32 reserved[4];
	__be32 crc;
};

struct thunderbolt_ip_login_response {
	struct thunderbolt_ip_header header;
	__be32 status;
	__be32 receiver_mac_address[2];
	__be32 receiver_mac_address_length;
	__be32 reserved[4];
	__be32 crc;
};

struct thunderbolt_ip_logout {
	struct thunderbolt_ip_header header;
	__be32 crc;
};

struct thunderbolt_ip_status {
	struct thunderbolt_ip_header header;
	__be32 status;
	__be32 crc;
};

struct approve_inter_domain_connection_cmd {
	__be32 req_code;
	__be32 attributes;
#define AIDC_ATTR_LINK_SHIFT	16
#define AIDC_ATTR_LINK_MASK	GENMASK(18, AIDC_ATTR_LINK_SHIFT)
#define AIDC_ATTR_DEPTH_SHIFT	20
#define AIDC_ATTR_DEPTH_MASK	GENMASK(23, AIDC_ATTR_DEPTH_SHIFT)
	uuid_be remote_uuid;
	__be16 transmit_ring_number;
	__be16 transmit_path;
	__be16 receive_ring_number;
	__be16 receive_path;
	__be32 crc;
};

enum neg_event {
	RECEIVE_LOGOUT = NUM_MEDIUM_STATUSES,
	RECEIVE_LOGIN_RESPONSE,
	RECEIVE_LOGIN,
	NUM_NEG_EVENTS
};

enum disconnect_path_stage {
	STAGE_1 = BIT(0),
	STAGE_2 = BIT(1)
};

/**
 *  struct tbt_port - the basic tbt_port structure
 *  @tbt_nhi_ctxt:		context of the nhi controller.
 *  @net_dev:			networking device object.
 *  @login_retry_work:		work queue for sending login requests.
 *  @login_response_work:	work queue for sending login responses.
 *  @work_struct logout_work:	work queue for sending logout requests.
 *  @status_reply_work:		work queue for sending logout replies.
 *  @approve_inter_domain_work:	work queue for sending interdomain to icm.
 *  @route_str:			allows to route the messages to destination.
 *  @interdomain_local_uuid:	allows to route the messages from local source.
 *  @interdomain_remote_uuid:	allows to route the messages to destination.
 *  @command_id			a number that identifies the command.
 *  @negotiation_status:	holds the network negotiation state.
 *  @msg_enable:		used for debugging filters.
 *  @seq_num:			a number that identifies the session.
 *  @login_retry_count:		counts number of login retries sent.
 *  @local_depth:		depth of the remote peer in the chain.
 *  @transmit_path:		routing parameter for the icm.
 *  @frame_id:			counting ID of frames.
 *  @num:			port number.
 *  @local_path:		routing parameter for the icm.
 *  @enable_full_e2e:		whether to enable full E2E.
 *  @match_frame_id:		whether to match frame id on incoming packets.
 */
struct tbt_port {
	struct tbt_nhi_ctxt *nhi_ctxt;
	struct net_device *net_dev;
	struct delayed_work login_retry_work;
	struct work_struct login_response_work;
	struct work_struct logout_work;
	struct work_struct status_reply_work;
	struct work_struct approve_inter_domain_work;
	struct route_string route_str;
	uuid_be interdomain_local_uuid;
	uuid_be interdomain_remote_uuid;
	u32 command_id;
	u16 negotiation_status;
	u16 msg_enable;
	u8 seq_num;
	u8 login_retry_count;
	u8 local_depth;
	u8 transmit_path;
	u16 frame_id;
	u8 num;
	u8 local_path;
	bool enable_full_e2e : 1;
	bool match_frame_id : 1;
};

static void disconnect_path(struct tbt_port *port,
			    enum disconnect_path_stage stage)
{
	u32 cmd = (DISCONNECT_PORT_A_INTER_DOMAIN_PATH + port->num);

	cmd <<= REG_INMAIL_CMD_CMD_SHIFT;
	cmd &= REG_INMAIL_CMD_CMD_MASK;
	cmd |= REG_INMAIL_CMD_REQUEST;

	mutex_lock(&port->nhi_ctxt->mailbox_mutex);
	if (!mutex_trylock(&port->nhi_ctxt->d0_exit_mailbox_mutex)) {
		netif_notice(port, link, port->net_dev, "controller id %#x is existing D0\n",
			     port->nhi_ctxt->id);
	} else {
		nhi_mailbox(port->nhi_ctxt, cmd, stage, false);

		port->nhi_ctxt->net_devices[port->num].medium_sts =
					MEDIUM_READY_FOR_CONNECTION;

		mutex_unlock(&port->nhi_ctxt->d0_exit_mailbox_mutex);
	}
	mutex_unlock(&port->nhi_ctxt->mailbox_mutex);
}

static void tbt_net_tear_down(struct net_device *net_dev, bool send_logout)
{
	struct tbt_port *port = netdev_priv(net_dev);
	void __iomem *iobase = port->nhi_ctxt->iobase;
	void __iomem *tx_reg = NULL;
	u32 tx_reg_val = 0;

	netif_carrier_off(net_dev);
	netif_stop_queue(net_dev);

	if (port->negotiation_status & BIT(MEDIUM_CONNECTED)) {
		void __iomem *rx_reg = iobase + REG_RX_OPTIONS_BASE +
		      (port->local_path * REG_OPTS_STEP);
		u32 rx_reg_val = ioread32(rx_reg) & ~REG_OPTS_E2E_EN;

		tx_reg = iobase + REG_TX_OPTIONS_BASE +
			 (port->local_path * REG_OPTS_STEP);
		tx_reg_val = ioread32(tx_reg) & ~REG_OPTS_E2E_EN;

		disconnect_path(port, STAGE_1);

		/* disable RX flow control  */
		iowrite32(rx_reg_val, rx_reg);
		/* disable TX flow control  */
		iowrite32(tx_reg_val, tx_reg);
		/* disable RX ring  */
		iowrite32(rx_reg_val & ~REG_OPTS_VALID, rx_reg);

		rx_reg = iobase + REG_RX_RING_BASE +
			 (port->local_path * REG_RING_STEP);
		iowrite32(0, rx_reg + REG_RING_PHYS_LO_OFFSET);
		iowrite32(0, rx_reg + REG_RING_PHYS_HI_OFFSET);
	}

	/* Stop login messages */
	cancel_delayed_work_sync(&port->login_retry_work);

	if (send_logout)
		queue_work(port->nhi_ctxt->net_workqueue, &port->logout_work);

	if (port->negotiation_status & BIT(MEDIUM_CONNECTED)) {
		unsigned long flags;

		/* wait for TX to finish */
		usleep_range(5 * USEC_PER_MSEC, 7 * USEC_PER_MSEC);
		/* disable TX ring  */
		iowrite32(tx_reg_val & ~REG_OPTS_VALID, tx_reg);

		disconnect_path(port, STAGE_2);

		spin_lock_irqsave(&port->nhi_ctxt->lock, flags);
		/* disable RX and TX interrupts */
		RING_INT_DISABLE_TX_RX(iobase, port->local_path,
				       port->nhi_ctxt->num_paths);
		spin_unlock_irqrestore(&port->nhi_ctxt->lock, flags);
	}
}

static inline int send_message(struct tbt_port *port, const char *func,
				enum pdf_value pdf, u32 msg_len,
				const void *msg)
{
	u32 crc_offset = msg_len - sizeof(__be32);
	__be32 *crc = (__be32 *)((u8 *)msg + crc_offset);
	bool is_intdom = (pdf == PDF_INTER_DOMAIN_RESPONSE);
	int res;

	*crc = cpu_to_be32(~__crc32c_le(~0, msg, crc_offset));
	res = down_timeout(&port->nhi_ctxt->send_sem,
			   msecs_to_jiffies(3 * MSEC_PER_SEC));
	if (res) {
		netif_err(port, link, port->net_dev, "%s: controller id %#x timeout on send semaphore\n",
			  func, port->nhi_ctxt->id);
		return res;
	}

	if (!mutex_trylock(&port->nhi_ctxt->d0_exit_send_mutex)) {
		up(&port->nhi_ctxt->send_sem);
		netif_notice(port, link, port->net_dev, "%s: controller id %#x is existing D0\n",
			     func, port->nhi_ctxt->id);
		return -ENODEV;
	}

	res = nhi_send_message(port->nhi_ctxt, pdf, msg_len, msg, is_intdom);

	mutex_unlock(&port->nhi_ctxt->d0_exit_send_mutex);
	if (res)
		up(&port->nhi_ctxt->send_sem);

	return res;
}

static void approve_inter_domain(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     approve_inter_domain_work);
	struct approve_inter_domain_connection_cmd approve_msg = {
		.req_code = cpu_to_be32(CC_APPROVE_INTER_DOMAIN_CONNECTION),
		.transmit_path = cpu_to_be16(LOGIN_TX_PATH),
	};
	u32 aidc = (L0_PORT_NUM(port->route_str.lo) << AIDC_ATTR_LINK_SHIFT) &
		    AIDC_ATTR_LINK_MASK;

	aidc |= (port->local_depth << AIDC_ATTR_DEPTH_SHIFT) &
		 AIDC_ATTR_DEPTH_MASK;

	approve_msg.attributes = cpu_to_be32(aidc);

	memcpy(&approve_msg.remote_uuid, &port->interdomain_remote_uuid,
	       sizeof(approve_msg.remote_uuid));
	approve_msg.transmit_ring_number = cpu_to_be16(port->local_path);
	approve_msg.receive_ring_number = cpu_to_be16(port->local_path);
	approve_msg.receive_path = cpu_to_be16(port->transmit_path);

	send_message(port, __func__, PDF_SW_TO_FW_COMMAND, sizeof(approve_msg),
		     &approve_msg);
}

static inline void prepare_header(struct thunderbolt_ip_header *header,
				  struct tbt_port *port,
				  enum thunderbolt_ip_packet_type packet_type,
				  u8 len_dwords)
{
	const uuid_be proto_uuid = APPLE_THUNDERBOLT_IP_PROTOCOL_UUID;

	header->packet_type = cpu_to_be32(packet_type);
	header->route_str.hi = cpu_to_be32(port->route_str.hi);
	header->route_str.lo = cpu_to_be32(port->route_str.lo);
	header->attributes = cpu_to_be32(
		((port->seq_num << HDR_ATTR_SEQ_NUM_SHIFT) &
		 HDR_ATTR_SEQ_NUM_MASK) |
		((len_dwords << HDR_ATTR_LEN_SHIFT) & HDR_ATTR_LEN_MASK));
	memcpy(&header->apple_tbt_ip_proto_uuid, &proto_uuid,
	       sizeof(header->apple_tbt_ip_proto_uuid));
	memcpy(&header->initiator_uuid, &port->interdomain_local_uuid,
	       sizeof(header->initiator_uuid));
	memcpy(&header->target_uuid, &port->interdomain_remote_uuid,
	       sizeof(header->target_uuid));
	header->command_id = cpu_to_be32(port->command_id);

	port->command_id++;
}

static void status_reply(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     status_reply_work);
	struct thunderbolt_ip_status status_msg = {
		.status = 0,
	};

	prepare_header(&status_msg.header, port,
		       THUNDERBOLT_IP_STATUS_TYPE,
		       (offsetof(struct thunderbolt_ip_status, crc) -
			offsetof(struct thunderbolt_ip_status,
				 header.apple_tbt_ip_proto_uuid)) /
		       sizeof(u32));

	send_message(port, __func__, PDF_INTER_DOMAIN_RESPONSE,
		     sizeof(status_msg), &status_msg);

}

static void logout(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     logout_work);
	struct thunderbolt_ip_logout logout_msg;

	prepare_header(&logout_msg.header, port,
		       THUNDERBOLT_IP_LOGOUT_TYPE,
		       (offsetof(struct thunderbolt_ip_logout, crc) -
			offsetof(struct thunderbolt_ip_logout,
			       header.apple_tbt_ip_proto_uuid)) / sizeof(u32));

	send_message(port, __func__, PDF_INTER_DOMAIN_RESPONSE,
		     sizeof(logout_msg), &logout_msg);

}

static void login_response(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     login_response_work);
	struct thunderbolt_ip_login_response login_res_msg = {
		.receiver_mac_address_length = cpu_to_be32(ETH_ALEN),
	};

	prepare_header(&login_res_msg.header, port,
		       THUNDERBOLT_IP_LOGIN_RESPONSE_TYPE,
		       (offsetof(struct thunderbolt_ip_login_response, crc) -
			offsetof(struct thunderbolt_ip_login_response,
			       header.apple_tbt_ip_proto_uuid)) / sizeof(u32));

	ether_addr_copy((u8 *)login_res_msg.receiver_mac_address,
			port->net_dev->dev_addr);

	send_message(port, __func__, PDF_INTER_DOMAIN_RESPONSE,
		     sizeof(login_res_msg), &login_res_msg);

}

static void login_retry(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     login_retry_work.work);
	struct thunderbolt_ip_login login_msg = {
		.protocol_revision = cpu_to_be32(
				APPLE_THUNDERBOLT_IP_PROTOCOL_REVISION),
		.transmit_path = cpu_to_be32(LOGIN_TX_PATH),
	};


	if (port->nhi_ctxt->d0_exit)
		return;

	port->login_retry_count++;

	prepare_header(&login_msg.header, port,
		       THUNDERBOLT_IP_LOGIN_TYPE,
		       (offsetof(struct thunderbolt_ip_login, crc) -
		       offsetof(struct thunderbolt_ip_login,
		       header.apple_tbt_ip_proto_uuid)) / sizeof(u32));

	if (send_message(port, __func__, PDF_INTER_DOMAIN_RESPONSE,
			 sizeof(login_msg), &login_msg) == -ENODEV)
		return;

	if (likely(port->login_retry_count < NUM_TX_LOGIN_RETRIES))
		queue_delayed_work(port->nhi_ctxt->net_workqueue,
				   &port->login_retry_work,
				   msecs_to_jiffies(5 * MSEC_PER_SEC));
	else
		netif_notice(port, link, port->net_dev, "port %u (%#x) login timeout after %u retries\n",
			     port->num, port->negotiation_status,
			     port->login_retry_count);
}

void negotiation_events(struct net_device *net_dev,
			enum medium_status medium_sts)
{
	struct tbt_port *port = netdev_priv(net_dev);
	void __iomem *iobase = port->nhi_ctxt->iobase;
	u32 sof_eof_en, tx_ring_conf, rx_ring_conf, e2e_en;
	void __iomem *reg;
	unsigned long flags;
	u16 hop_id;
	bool send_logout;

	if (!netif_running(net_dev)) {
		netif_dbg(port, link, net_dev, "port %u (%#x) is down\n",
			  port->num, port->negotiation_status);
		return;
	}

	netif_dbg(port, link, net_dev, "port %u (%#x) receive event %u\n",
		  port->num, port->negotiation_status, medium_sts);

	switch (medium_sts) {
	case MEDIUM_DISCONNECTED:
		send_logout = (port->negotiation_status
				& (BIT(MEDIUM_CONNECTED)
				   |  BIT(MEDIUM_READY_FOR_CONNECTION)));
		send_logout = send_logout && !(port->negotiation_status &
					       BIT(RECEIVE_LOGOUT));

		tbt_net_tear_down(net_dev, send_logout);
		port->negotiation_status = BIT(MEDIUM_DISCONNECTED);
		break;

	case MEDIUM_CONNECTED:
		/*
		 * check if meanwhile other side sent logout
		 * if yes, just don't allow connection to take place
		 * and disconnect path
		 */
		if (port->negotiation_status & BIT(RECEIVE_LOGOUT)) {
			disconnect_path(port, STAGE_1 | STAGE_2);
			break;
		}

		port->negotiation_status = BIT(MEDIUM_CONNECTED);

		/* configure TX ring */
		reg = iobase + REG_TX_RING_BASE +
		      (port->local_path * REG_RING_STEP);

		tx_ring_conf = (TBT_NET_NUM_TX_BUFS << REG_RING_SIZE_SHIFT) &
				REG_RING_SIZE_MASK;

		iowrite32(tx_ring_conf, reg + REG_RING_SIZE_OFFSET);

		/* enable the rings */
		reg = iobase + REG_TX_OPTIONS_BASE +
		      (port->local_path * REG_OPTS_STEP);
		if (port->enable_full_e2e) {
			iowrite32(REG_OPTS_VALID | REG_OPTS_E2E_EN, reg);
			hop_id = port->local_path;
		} else {
			iowrite32(REG_OPTS_VALID, reg);
			hop_id = TBT_EXIST_BUT_UNUSED_HOPID;
		}

		reg = iobase + REG_RX_OPTIONS_BASE +
		      (port->local_path * REG_OPTS_STEP);

		sof_eof_en = (BIT(PDF_TBT_NET_START_OF_FRAME) <<
			      REG_RX_OPTS_MASK_SOF_SHIFT) &
			     REG_RX_OPTS_MASK_SOF_MASK;

		sof_eof_en |= (BIT(PDF_TBT_NET_END_OF_FRAME) <<
			       REG_RX_OPTS_MASK_EOF_SHIFT) &
			      REG_RX_OPTS_MASK_EOF_MASK;

		iowrite32(sof_eof_en, reg + REG_RX_OPTS_MASK_OFFSET);

		e2e_en = REG_OPTS_VALID | REG_OPTS_E2E_EN;
		e2e_en |= (hop_id << REG_RX_OPTS_TX_E2E_HOP_ID_SHIFT) &
			  REG_RX_OPTS_TX_E2E_HOP_ID_MASK;

		iowrite32(e2e_en, reg);

		/*
		 * Configure RX ring
		 * must be after enable ring for E2E to work
		 */
		reg = iobase + REG_RX_RING_BASE +
		      (port->local_path * REG_RING_STEP);

		rx_ring_conf = (TBT_NET_NUM_RX_BUFS << REG_RING_SIZE_SHIFT) &
				REG_RING_SIZE_MASK;

		rx_ring_conf |= (TBT_RING_MAX_FRAME_SIZE <<
				 REG_RING_BUF_SIZE_SHIFT) &
				REG_RING_BUF_SIZE_MASK;

		iowrite32(rx_ring_conf, reg + REG_RING_SIZE_OFFSET);

		spin_lock_irqsave(&port->nhi_ctxt->lock, flags);
		/* enable RX interrupt */
		iowrite32(ioread32(iobase + REG_RING_INTERRUPT_BASE) |
			  REG_RING_INT_RX_PROCESSED(port->local_path,
						    port->nhi_ctxt->num_paths),
			  iobase + REG_RING_INTERRUPT_BASE);
		spin_unlock_irqrestore(&port->nhi_ctxt->lock, flags);

		netif_info(port, link, net_dev, "Thunderbolt(TM) Networking port %u - ready\n",
			   port->num);

		netif_carrier_on(net_dev);
		netif_start_queue(net_dev);
		break;

	case MEDIUM_READY_FOR_CONNECTION:
		/*
		 * If medium is connected, no reason to go back,
		 * keep it 'connected'.
		 * If received login response, don't need to trigger login
		 * retries again.
		 */
		if (unlikely(port->negotiation_status &
			     (BIT(MEDIUM_CONNECTED) |
			      BIT(RECEIVE_LOGIN_RESPONSE))))
			break;

		port->negotiation_status = BIT(MEDIUM_READY_FOR_CONNECTION);
		port->login_retry_count = 0;
		queue_delayed_work(port->nhi_ctxt->net_workqueue,
				   &port->login_retry_work, 0);
		break;

	default:
		break;
	}
}

void negotiation_messages(struct net_device *net_dev,
			  struct thunderbolt_ip_header *hdr)
{
	struct tbt_port *port = netdev_priv(net_dev);
	__be32 status;

	if (!netif_running(net_dev)) {
		netif_dbg(port, link, net_dev, "port %u (%#x) is down\n",
			  port->num, port->negotiation_status);
		return;
	}

	switch (hdr->packet_type) {
	case cpu_to_be32(THUNDERBOLT_IP_LOGIN_TYPE):
		port->transmit_path = be32_to_cpu(
			((struct thunderbolt_ip_login *)hdr)->transmit_path);
		netif_dbg(port, link, net_dev, "port %u (%#x) receive ThunderboltIP login message with transmit path %u\n",
			  port->num, port->negotiation_status,
			  port->transmit_path);

		if (unlikely(port->negotiation_status &
			     BIT(MEDIUM_DISCONNECTED)))
			break;

		queue_work(port->nhi_ctxt->net_workqueue,
			   &port->login_response_work);

		if (unlikely(port->negotiation_status & BIT(MEDIUM_CONNECTED)))
			break;

		/*
		 *  In case a login response received from other peer
		 * on my login and acked their login for the first time,
		 * so just approve the inter-domain now
		 */
		if (port->negotiation_status & BIT(RECEIVE_LOGIN_RESPONSE)) {
			if (!(port->negotiation_status & BIT(RECEIVE_LOGIN)))
				queue_work(port->nhi_ctxt->net_workqueue,
					   &port->approve_inter_domain_work);
		/*
		 * if we reached the number of max retries or previous
		 * logout, schedule another round of login retries
		 */
		} else if ((port->login_retry_count >= NUM_TX_LOGIN_RETRIES) ||
			   (port->negotiation_status & BIT(RECEIVE_LOGOUT))) {
			port->negotiation_status &= ~(BIT(RECEIVE_LOGOUT));
			port->login_retry_count = 0;
			queue_delayed_work(port->nhi_ctxt->net_workqueue,
					   &port->login_retry_work, 0);
		}

		port->negotiation_status |= BIT(RECEIVE_LOGIN);

		break;

	case cpu_to_be32(THUNDERBOLT_IP_LOGIN_RESPONSE_TYPE):
		status = ((struct thunderbolt_ip_login_response *)hdr)->status;
		if (likely(status == 0)) {
			netif_dbg(port, link, net_dev, "port %u (%#x) receive ThunderboltIP login response message\n",
				  port->num,
				  port->negotiation_status);

			if (unlikely(port->negotiation_status &
				     (BIT(MEDIUM_DISCONNECTED) |
				      BIT(MEDIUM_CONNECTED) |
				      BIT(RECEIVE_LOGIN_RESPONSE))))
				break;

			port->negotiation_status |=
						BIT(RECEIVE_LOGIN_RESPONSE);
			cancel_delayed_work_sync(&port->login_retry_work);
			/*
			 * login was received from other peer and now response
			 * on our login so approve the inter-domain
			 */
			if (port->negotiation_status & BIT(RECEIVE_LOGIN))
				queue_work(port->nhi_ctxt->net_workqueue,
					   &port->approve_inter_domain_work);
			else
				port->negotiation_status &=
							~BIT(RECEIVE_LOGOUT);
		} else {
			netif_notice(port, link, net_dev, "port %u (%#x) receive ThunderboltIP login response message with status %u\n",
				     port->num,
				     port->negotiation_status,
				     be32_to_cpu(status));
		}
		break;

	case cpu_to_be32(THUNDERBOLT_IP_LOGOUT_TYPE):
		netif_dbg(port, link, net_dev, "port %u (%#x) receive ThunderboltIP logout message\n",
			  port->num, port->negotiation_status);

		queue_work(port->nhi_ctxt->net_workqueue,
			   &port->status_reply_work);
		port->negotiation_status &= ~(BIT(RECEIVE_LOGIN) |
					      BIT(RECEIVE_LOGIN_RESPONSE));
		port->negotiation_status |= BIT(RECEIVE_LOGOUT);

		if (!(port->negotiation_status & BIT(MEDIUM_CONNECTED))) {
			tbt_net_tear_down(net_dev, false);
			break;
		}

		tbt_net_tear_down(net_dev, true);

		port->negotiation_status |= BIT(MEDIUM_READY_FOR_CONNECTION);
		port->negotiation_status &= ~(BIT(MEDIUM_CONNECTED));
		break;

	case cpu_to_be32(THUNDERBOLT_IP_STATUS_TYPE):
		netif_dbg(port, link, net_dev, "port %u (%#x) receive ThunderboltIP status message with status %u\n",
			  port->num, port->negotiation_status,
			  be32_to_cpu(
			  ((struct thunderbolt_ip_status *)hdr)->status));
		break;
	}
}

void nhi_dealloc_etherdev(struct net_device *net_dev)
{
	unregister_netdev(net_dev);
	free_netdev(net_dev);
}

void nhi_update_etherdev(struct tbt_nhi_ctxt *nhi_ctxt,
			 struct net_device *net_dev, struct genl_info *info)
{
	struct tbt_port *port = netdev_priv(net_dev);

	nla_memcpy(&(port->route_str),
		   info->attrs[NHI_ATTR_LOCAL_ROUTE_STRING],
		   sizeof(port->route_str));
	nla_memcpy(&port->interdomain_remote_uuid,
		   info->attrs[NHI_ATTR_REMOTE_UUID],
		   sizeof(port->interdomain_remote_uuid));
	port->local_depth = nla_get_u8(info->attrs[NHI_ATTR_LOCAL_DEPTH]);
	port->enable_full_e2e = nhi_ctxt->support_full_e2e ?
		nla_get_flag(info->attrs[NHI_ATTR_ENABLE_FULL_E2E]) : false;
	port->match_frame_id =
		nla_get_flag(info->attrs[NHI_ATTR_MATCH_FRAME_ID]);
	port->frame_id = 0;
}

struct net_device *nhi_alloc_etherdev(struct tbt_nhi_ctxt *nhi_ctxt,
				      u8 port_num, struct genl_info *info)
{
	struct tbt_port *port;
	struct net_device *net_dev = alloc_etherdev(sizeof(struct tbt_port));
	u32 hash;

	if (!net_dev)
		return NULL;

	SET_NETDEV_DEV(net_dev, &nhi_ctxt->pdev->dev);

	port = netdev_priv(net_dev);
	port->nhi_ctxt = nhi_ctxt;
	port->net_dev = net_dev;
	nla_memcpy(&port->interdomain_local_uuid,
		   info->attrs[NHI_ATTR_LOCAL_UUID],
		   sizeof(port->interdomain_local_uuid));
	nhi_update_etherdev(nhi_ctxt, net_dev, info);
	port->num = port_num;
	port->local_path = PATH_FROM_PORT(nhi_ctxt->num_paths, port_num);

	port->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

	net_dev->addr_assign_type = NET_ADDR_PERM;
	/* unicast and locally administred MAC */
	net_dev->dev_addr[0] = (port_num << 4) | 0x02;
	hash = jhash2((u32 *)&port->interdomain_local_uuid,
		      sizeof(port->interdomain_local_uuid)/sizeof(u32), 0);

	memcpy(net_dev->dev_addr + 1, &hash, sizeof(hash));
	hash = jhash2((u32 *)&port->interdomain_local_uuid,
		      sizeof(port->interdomain_local_uuid)/sizeof(u32), hash);

	net_dev->dev_addr[5] = hash & 0xff;

	scnprintf(net_dev->name, sizeof(net_dev->name), "tbtnet%%dp%hhu",
		  port_num);

	INIT_DELAYED_WORK(&port->login_retry_work, login_retry);
	INIT_WORK(&port->login_response_work, login_response);
	INIT_WORK(&port->logout_work, logout);
	INIT_WORK(&port->status_reply_work, status_reply);
	INIT_WORK(&port->approve_inter_domain_work, approve_inter_domain);

	netif_info(port, probe, net_dev,
		   "Thunderbolt(TM) Networking port %u - MAC Address: %pM\n",
		   port_num, net_dev->dev_addr);

	return net_dev;
}
