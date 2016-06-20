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

#include <linux/printk.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include "icm_nhi.h"
#include "net.h"

#define NHI_GENL_VERSION 1
#define NHI_GENL_NAME "thunderbolt"

#define DEVICE_DATA(num_ports, dma_port, nvm_ver_offset, nvm_auth_on_boot,\
		    support_full_e2e) \
	((num_ports) | ((dma_port) << 4) | ((nvm_ver_offset) << 10) | \
	 ((nvm_auth_on_boot) << 22) | ((support_full_e2e) << 23))
#define DEVICE_DATA_NUM_PORTS(device_data) ((device_data) & 0xf)
#define DEVICE_DATA_DMA_PORT(device_data) (((device_data) >> 4) & 0x3f)
#define DEVICE_DATA_NVM_VER_OFFSET(device_data) (((device_data) >> 10) & 0xfff)
#define DEVICE_DATA_NVM_AUTH_ON_BOOT(device_data) (((device_data) >> 22) & 0x1)
#define DEVICE_DATA_SUPPORT_FULL_E2E(device_data) (((device_data) >> 23) & 0x1)

#define USEC_TO_256_NSECS(usec) DIV_ROUND_UP((usec) * NSEC_PER_USEC, 256)

/* NHI genetlink commands */
enum {
	NHI_CMD_UNSPEC,
	NHI_CMD_SUBSCRIBE,
	NHI_CMD_UNSUBSCRIBE,
	NHI_CMD_QUERY_INFORMATION,
	NHI_CMD_MSG_TO_ICM,
	NHI_CMD_MSG_FROM_ICM,
	NHI_CMD_MAILBOX,
	NHI_CMD_APPROVE_TBT_NETWORKING,
	NHI_CMD_ICM_IN_SAFE_MODE,
	__NHI_CMD_MAX,
};
#define NHI_CMD_MAX (__NHI_CMD_MAX - 1)

static struct genl_family nhi_genl_family;

static LIST_HEAD(controllers_list);
static DEFINE_MUTEX(controllers_list_mutex);
static atomic_t subscribers = ATOMIC_INIT(0);
/*
 * Some of the received generic netlink messages are replied in a different
 * context. The reply has to include the netlink portid of sender, therefore
 * saving it in global variable (current assuption is one sender).
 */
static u32 portid;

static bool nhi_nvm_authenticated(struct tbt_nhi_ctxt *nhi_ctxt)
{
	enum icm_operation_mode op_mode;
	u32 *msg_head, port_id, reg;
	struct sk_buff *skb;
	int i;

	if (!nhi_ctxt->nvm_auth_on_boot)
		return true;

	/*
	 * The check for NVM authentication can take time for iCM,
	 * especially in low power configuration.
	 */
	for (i = 0; i < 5; i++) {
		u32 status = ioread32(nhi_ctxt->iobase + REG_FW_STS);

		if (status & REG_FW_STS_NVM_AUTH_DONE)
			break;

		msleep(30);
	}
	/*
	 * The check for authentication is done after checking if iCM
	 * is present so it shouldn't reach the max tries (=5).
	 * Anyway, the check for full functionality below covers the error case.
	 */
	reg = ioread32(nhi_ctxt->iobase + REG_OUTMAIL_CMD);
	op_mode = (reg & REG_OUTMAIL_CMD_OP_MODE_MASK) >>
		  REG_OUTMAIL_CMD_OP_MODE_SHIFT;
	if (op_mode == FULL_FUNCTIONALITY)
		return true;

	dev_warn(&nhi_ctxt->pdev->dev, "controller id %#x is in operation mode %#x status %#lx, NVM image update might be required\n",
		 nhi_ctxt->id, op_mode,
		 (reg & REG_OUTMAIL_CMD_STS_MASK)>>REG_OUTMAIL_CMD_STS_SHIFT);

	skb = genlmsg_new(NLMSG_ALIGN(nhi_genl_family.hdrsize), GFP_KERNEL);
	if (!skb) {
		dev_err(&nhi_ctxt->pdev->dev, "genlmsg_new failed: not enough memory to send controller operational mode\n");
		return false;
	}

	/* keeping port_id into a local variable for next use */
	port_id = portid;
	msg_head = genlmsg_put(skb, port_id, 0, &nhi_genl_family, 0,
			       NHI_CMD_ICM_IN_SAFE_MODE);
	if (!msg_head) {
		nlmsg_free(skb);
		dev_err(&nhi_ctxt->pdev->dev, "genlmsg_put failed: not enough memory to send controller operational mode\n");
		return false;
	}

	*msg_head = nhi_ctxt->id;

	genlmsg_end(skb, msg_head);

	genlmsg_unicast(&init_net, skb, port_id);

	return false;
}

int nhi_send_message(struct tbt_nhi_ctxt *nhi_ctxt, enum pdf_value pdf,
		     u32 msg_len, const void *msg, bool ignore_icm_resp)
{
	u32 prod_cons, prod, cons, attr;
	struct tbt_icm_ring_shared_memory *shared_mem;
	void __iomem *reg = TBT_RING_CONS_PROD_REG(nhi_ctxt->iobase,
						   REG_TX_RING_BASE,
						   TBT_ICM_RING_NUM);

	if (nhi_ctxt->d0_exit)
		return -ENODEV;

	prod_cons = ioread32(reg);
	prod = TBT_REG_RING_PROD_EXTRACT(prod_cons);
	cons = TBT_REG_RING_CONS_EXTRACT(prod_cons);
	if (prod >= TBT_ICM_RING_NUM_TX_BUFS) {
		dev_warn(&nhi_ctxt->pdev->dev,
			 "controller id %#x is not functional, producer %u out of range\n",
			 nhi_ctxt->id, prod);
		return -ENODEV;
	}
	if (TBT_TX_RING_FULL(prod, cons, TBT_ICM_RING_NUM_TX_BUFS)) {
		dev_err(&nhi_ctxt->pdev->dev,
			"controller id %#x is not functional, TX ring full\n",
			nhi_ctxt->id);
		return -ENOSPC;
	}

	attr = (msg_len << DESC_ATTR_LEN_SHIFT) & DESC_ATTR_LEN_MASK;
	attr |= (pdf << DESC_ATTR_EOF_SHIFT) & DESC_ATTR_EOF_MASK;

	shared_mem = nhi_ctxt->icm_ring_shared_mem;
	shared_mem->tx_buf_desc[prod].attributes = cpu_to_le32(attr);

	memcpy(shared_mem->tx_buf[prod], msg, msg_len);

	prod_cons &= ~REG_RING_PROD_MASK;
	prod_cons |= (((prod + 1) % TBT_ICM_RING_NUM_TX_BUFS) <<
		      REG_RING_PROD_SHIFT) & REG_RING_PROD_MASK;

	nhi_ctxt->wait_for_icm_resp = true;
	nhi_ctxt->ignore_icm_resp = ignore_icm_resp;

	iowrite32(prod_cons, reg);

	return 0;
}

static int nhi_send_driver_ready_command(struct tbt_nhi_ctxt *nhi_ctxt)
{
	struct driver_ready_command {
		__be32 req_code;
		__be32 crc;
	} drv_rdy_cmd = {
		.req_code = cpu_to_be32(CC_DRV_READY),
	};
	u32 crc32;

	crc32 = __crc32c_le(~0, (unsigned char const *)&drv_rdy_cmd,
			    offsetof(struct driver_ready_command, crc));

	drv_rdy_cmd.crc = cpu_to_be32(~crc32);

	return nhi_send_message(nhi_ctxt, PDF_SW_TO_FW_COMMAND,
				sizeof(drv_rdy_cmd), &drv_rdy_cmd, false);
}

/**
 * nhi_search_ctxt - search by id the controllers_list.
 * Should be called under controllers_list_mutex.
 *
 * @id: id of the controller
 *
 * Return: driver context if found, NULL otherwise.
 */
static struct tbt_nhi_ctxt *nhi_search_ctxt(u32 id)
{
	struct tbt_nhi_ctxt *nhi_ctxt;

	list_for_each_entry(nhi_ctxt, &controllers_list, node)
		if (nhi_ctxt->id == id)
			return nhi_ctxt;

	return NULL;
}

static int nhi_genl_subscribe(__always_unused struct sk_buff *u_skb,
			      struct genl_info *info)
			      __acquires(&nhi_ctxt->send_sem)
{
	struct tbt_nhi_ctxt *nhi_ctxt;

	/*
	 * To send driver ready command to iCM, need at least one subscriber
	 * that will handle the response.
	 * Currently the assumption is one user mode daemon as subscriber
	 * so one portid global variable (without locking).
	 */
	if (atomic_inc_return(&subscribers) >= 1) {
		portid = info->snd_portid;
		if (mutex_lock_interruptible(&controllers_list_mutex)) {
			atomic_dec_if_positive(&subscribers);
			return -ERESTART;
		}
		list_for_each_entry(nhi_ctxt, &controllers_list, node) {
			int res;

			if (nhi_ctxt->d0_exit ||
			    !nhi_nvm_authenticated(nhi_ctxt))
				continue;

			res = down_timeout(&nhi_ctxt->send_sem,
					   msecs_to_jiffies(10*MSEC_PER_SEC));
			if (res) {
				dev_err(&nhi_ctxt->pdev->dev,
					"%s: controller id %#x is not functional, timeout on waiting for FW response to previous message\n",
					__func__, nhi_ctxt->id);
				continue;
			}

			if (!mutex_trylock(&nhi_ctxt->d0_exit_send_mutex)) {
				up(&nhi_ctxt->send_sem);
				continue;
			}

			res = nhi_send_driver_ready_command(nhi_ctxt);

			mutex_unlock(&nhi_ctxt->d0_exit_send_mutex);
			if (res)
				up(&nhi_ctxt->send_sem);
		}
		mutex_unlock(&controllers_list_mutex);
	}

	return 0;
}

static int nhi_genl_unsubscribe(__always_unused struct sk_buff *u_skb,
				__always_unused struct genl_info *info)
{
	atomic_dec_if_positive(&subscribers);

	return 0;
}

static int nhi_genl_query_information(__always_unused struct sk_buff *u_skb,
				      struct genl_info *info)
{
	struct tbt_nhi_ctxt *nhi_ctxt;
	struct sk_buff *skb;
	int res = -ENODEV;
	u32 *msg_head;

	if (!info || !info->userhdr)
		return -EINVAL;

	skb = genlmsg_new(NLMSG_ALIGN(nhi_genl_family.hdrsize) +
			  nla_total_size(sizeof(DRV_VERSION)) +
			  nla_total_size(sizeof(nhi_ctxt->nvm_ver_offset)) +
			  nla_total_size(sizeof(nhi_ctxt->num_ports)) +
			  nla_total_size(sizeof(nhi_ctxt->dma_port)) +
			  nla_total_size(0),	/* nhi_ctxt->support_full_e2e */
			  GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	msg_head = genlmsg_put_reply(skb, info, &nhi_genl_family, 0,
				     NHI_CMD_QUERY_INFORMATION);
	if (!msg_head) {
		res = -ENOMEM;
		goto genl_put_reply_failure;
	}

	if (mutex_lock_interruptible(&controllers_list_mutex)) {
		res = -ERESTART;
		goto genl_put_reply_failure;
	}

	nhi_ctxt = nhi_search_ctxt(*(u32 *)info->userhdr);
	if (nhi_ctxt && !nhi_ctxt->d0_exit) {
		bool msg_too_long;

		*msg_head = nhi_ctxt->id;

		msg_too_long = !!nla_put_string(skb, NHI_ATTR_DRV_VERSION,
						DRV_VERSION);

		msg_too_long = msg_too_long ||
			       nla_put_u16(skb, NHI_ATTR_NVM_VER_OFFSET,
					   nhi_ctxt->nvm_ver_offset);

		msg_too_long = msg_too_long ||
			       nla_put_u8(skb, NHI_ATTR_NUM_PORTS,
					  nhi_ctxt->num_ports);

		msg_too_long = msg_too_long ||
			       nla_put_u8(skb, NHI_ATTR_DMA_PORT,
					  nhi_ctxt->dma_port);

		if (msg_too_long) {
			res = -EMSGSIZE;
			goto release_ctl_list_lock;
		}

		if (nhi_ctxt->support_full_e2e &&
		    nla_put_flag(skb, NHI_ATTR_SUPPORT_FULL_E2E)) {
			res = -EMSGSIZE;
			goto release_ctl_list_lock;
		}
		mutex_unlock(&controllers_list_mutex);

		genlmsg_end(skb, msg_head);

		return genlmsg_reply(skb, info);
	}

release_ctl_list_lock:
	mutex_unlock(&controllers_list_mutex);
	genlmsg_cancel(skb, msg_head);

genl_put_reply_failure:
	nlmsg_free(skb);

	return res;
}

static int nhi_genl_msg_to_icm(__always_unused struct sk_buff *u_skb,
			       struct genl_info *info)
			       __acquires(&nhi_ctxt->send_sem)
{
	struct tbt_nhi_ctxt *nhi_ctxt;
	int res = -ENODEV;
	int msg_len;
	void *msg;

	if (!info || !info->userhdr || !info->attrs ||
	    !info->attrs[NHI_ATTR_PDF] || !info->attrs[NHI_ATTR_MSG_TO_ICM])
		return -EINVAL;

	msg_len = nla_len(info->attrs[NHI_ATTR_MSG_TO_ICM]);
	if (msg_len > TBT_ICM_RING_MAX_FRAME_SIZE)
		return -ENOBUFS;

	msg = nla_data(info->attrs[NHI_ATTR_MSG_TO_ICM]);

	if (mutex_lock_interruptible(&controllers_list_mutex))
		return -ERESTART;

	nhi_ctxt = nhi_search_ctxt(*(u32 *)info->userhdr);
	if (nhi_ctxt && !nhi_ctxt->d0_exit) {
		/*
		 * waiting 10 seconds to receive a FW response
		 * if not, just give up and pop up an error
		 */
		res = down_timeout(&nhi_ctxt->send_sem,
				   msecs_to_jiffies(10 * MSEC_PER_SEC));
		if (res) {
			void __iomem *rx_prod_cons = TBT_RING_CONS_PROD_REG(
							nhi_ctxt->iobase,
							REG_RX_RING_BASE,
							TBT_ICM_RING_NUM);
			void __iomem *tx_prod_cons = TBT_RING_CONS_PROD_REG(
							nhi_ctxt->iobase,
							REG_TX_RING_BASE,
							TBT_ICM_RING_NUM);
			dev_err(&nhi_ctxt->pdev->dev,
				"%s: controller id %#x is not functional, timeout on waiting for FW response to previous message, tx prod&cons=%#x, rx prod&cons=%#x\n",
				__func__, nhi_ctxt->id, ioread32(tx_prod_cons),
				ioread32(rx_prod_cons));
			goto release_ctl_list_lock;
		}

		if (!mutex_trylock(&nhi_ctxt->d0_exit_send_mutex)) {
			up(&nhi_ctxt->send_sem);
			goto release_ctl_list_lock;
		}

		mutex_unlock(&controllers_list_mutex);

		res = nhi_send_message(nhi_ctxt,
				       nla_get_u32(info->attrs[NHI_ATTR_PDF]),
				       msg_len, msg, false);

		mutex_unlock(&nhi_ctxt->d0_exit_send_mutex);
		if (res)
			up(&nhi_ctxt->send_sem);

		return res;
	}

release_ctl_list_lock:
	mutex_unlock(&controllers_list_mutex);
	return res;
}

int nhi_mailbox(struct tbt_nhi_ctxt *nhi_ctxt, u32 cmd, u32 data, bool deinit)
{
	u32 delay = deinit ? U32_C(20) : U32_C(100);
	int i;

	iowrite32(data, nhi_ctxt->iobase + REG_INMAIL_DATA);
	iowrite32(cmd, nhi_ctxt->iobase + REG_INMAIL_CMD);

#define NHI_INMAIL_CMD_RETRIES 50
	/*
	 * READ_ONCE fetches the value of nhi_ctxt->d0_exit every time
	 * and avoid optimization.
	 * deinit = true to continue the loop even if D3 process has been
	 * carried out.
	 */
	for (i = 0; (i < NHI_INMAIL_CMD_RETRIES) &&
		    (deinit || !READ_ONCE(nhi_ctxt->d0_exit)); i++) {
		cmd = ioread32(nhi_ctxt->iobase + REG_INMAIL_CMD);

		if (cmd & REG_INMAIL_CMD_ERROR)
			return -EIO;

		if (!(cmd & REG_INMAIL_CMD_REQUEST))
			break;

		msleep(delay);
	}

	if (i == NHI_INMAIL_CMD_RETRIES) {
		if (!deinit)
			dev_err(&nhi_ctxt->pdev->dev,
				"controller id %#x is not functional, inmail timeout\n",
				nhi_ctxt->id);
		return -ETIMEDOUT;
	}

	return 0;
}

static inline bool nhi_is_path_disconnected(u32 cmd, u8 num_ports)
{
	return (cmd >= DISCONNECT_PORT_A_INTER_DOMAIN_PATH &&
		cmd < (DISCONNECT_PORT_A_INTER_DOMAIN_PATH + num_ports));
}

static int nhi_mailbox_disconn_path(struct tbt_nhi_ctxt *nhi_ctxt, u32 cmd)
	__releases(&controllers_list_mutex)
{
	struct port_net_dev *port;
	u32 port_num = cmd - DISCONNECT_PORT_A_INTER_DOMAIN_PATH;

	port = &(nhi_ctxt->net_devices[port_num]);
	mutex_lock(&port->state_mutex);

	mutex_unlock(&controllers_list_mutex);
	port->medium_sts = MEDIUM_READY_FOR_APPROVAL;
	if (port->net_dev)
		negotiation_events(port->net_dev, MEDIUM_DISCONNECTED);
	mutex_unlock(&port->state_mutex);
	return  0;
}

static int nhi_mailbox_generic(struct tbt_nhi_ctxt *nhi_ctxt, u32 mb_cmd)
	__releases(&controllers_list_mutex)
{
	int res = -ENODEV;

	if (mutex_lock_interruptible(&nhi_ctxt->mailbox_mutex)) {
		res = -ERESTART;
		goto release_ctl_list_lock;
	}

	if (!mutex_trylock(&nhi_ctxt->d0_exit_mailbox_mutex)) {
		mutex_unlock(&nhi_ctxt->mailbox_mutex);
		goto release_ctl_list_lock;
	}

	mutex_unlock(&controllers_list_mutex);

	res = nhi_mailbox(nhi_ctxt, mb_cmd, 0, false);
	mutex_unlock(&nhi_ctxt->d0_exit_mailbox_mutex);
	mutex_unlock(&nhi_ctxt->mailbox_mutex);

	return res;

release_ctl_list_lock:
	mutex_unlock(&controllers_list_mutex);
	return res;
}

static int nhi_genl_mailbox(__always_unused struct sk_buff *u_skb,
			    struct genl_info *info)
{
	struct tbt_nhi_ctxt *nhi_ctxt;
	u32 cmd, mb_cmd;

	if (!info || !info->userhdr || !info->attrs ||
	    !info->attrs[NHI_ATTR_MAILBOX_CMD])
		return -EINVAL;

	cmd = nla_get_u32(info->attrs[NHI_ATTR_MAILBOX_CMD]);
	mb_cmd = ((cmd << REG_INMAIL_CMD_CMD_SHIFT) &
		  REG_INMAIL_CMD_CMD_MASK) | REG_INMAIL_CMD_REQUEST;

	if (mutex_lock_interruptible(&controllers_list_mutex))
		return -ERESTART;

	nhi_ctxt = nhi_search_ctxt(*(u32 *)info->userhdr);
	if (nhi_ctxt && !nhi_ctxt->d0_exit) {

		/* rwsem is released later by the below functions */
		if (nhi_is_path_disconnected(cmd, nhi_ctxt->num_ports))
			return nhi_mailbox_disconn_path(nhi_ctxt, cmd);
		else
			return nhi_mailbox_generic(nhi_ctxt, mb_cmd);

	}

	mutex_unlock(&controllers_list_mutex);
	return -ENODEV;
}

static int nhi_genl_approve_networking(__always_unused struct sk_buff *u_skb,
				       struct genl_info *info)
{
	struct tbt_nhi_ctxt *nhi_ctxt;
	struct route_string *route_str;
	int res = -ENODEV;
	u8 port_num;

	if (!info || !info->userhdr || !info->attrs ||
	    !info->attrs[NHI_ATTR_LOCAL_ROUTE_STRING] ||
	    !info->attrs[NHI_ATTR_LOCAL_UUID] ||
	    !info->attrs[NHI_ATTR_REMOTE_UUID] ||
	    !info->attrs[NHI_ATTR_LOCAL_DEPTH])
		return -EINVAL;

	/*
	 * route_str is an unique topological address
	 * used for approving remote controller
	 */
	route_str = nla_data(info->attrs[NHI_ATTR_LOCAL_ROUTE_STRING]);
	/* extracts the port we're connected to */
	port_num = PORT_NUM_FROM_LINK(L0_PORT_NUM(route_str->lo));

	if (mutex_lock_interruptible(&controllers_list_mutex))
		return -ERESTART;

	nhi_ctxt = nhi_search_ctxt(*(u32 *)info->userhdr);
	if (nhi_ctxt && !nhi_ctxt->d0_exit) {
		struct port_net_dev *port;

		if (port_num >= nhi_ctxt->num_ports) {
			res = -EINVAL;
			goto free_ctl_list;
		}

		port = &(nhi_ctxt->net_devices[port_num]);

		mutex_lock(&port->state_mutex);
		mutex_unlock(&controllers_list_mutex);

		if (port->medium_sts != MEDIUM_READY_FOR_APPROVAL)
			goto unlock;

		port->medium_sts = MEDIUM_READY_FOR_CONNECTION;

		if (!port->net_dev) {
			port->net_dev = nhi_alloc_etherdev(nhi_ctxt, port_num,
							   info);
			if (!port->net_dev) {
				mutex_unlock(&port->state_mutex);
				return -ENOMEM;
			}
		} else {
			nhi_update_etherdev(nhi_ctxt, port->net_dev, info);

			negotiation_events(port->net_dev,
					   MEDIUM_READY_FOR_CONNECTION);
		}

unlock:
		mutex_unlock(&port->state_mutex);

		return 0;
	}

free_ctl_list:
	mutex_unlock(&controllers_list_mutex);

	return res;
}

static int nhi_genl_send_msg(struct tbt_nhi_ctxt *nhi_ctxt, enum pdf_value pdf,
			     const u8 *msg, u32 msg_len)
{
	u32 *msg_head, port_id;
	struct sk_buff *skb;
	int res;

	if (atomic_read(&subscribers) < 1)
		return -ENOTCONN;

	skb = genlmsg_new(NLMSG_ALIGN(nhi_genl_family.hdrsize) +
			  nla_total_size(msg_len) +
			  nla_total_size(sizeof(pdf)),
			  GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	port_id = portid;
	msg_head = genlmsg_put(skb, port_id, 0, &nhi_genl_family, 0,
			       NHI_CMD_MSG_FROM_ICM);
	if (!msg_head) {
		res = -ENOMEM;
		goto genl_put_reply_failure;
	}

	*msg_head = nhi_ctxt->id;

	if (nla_put_u32(skb, NHI_ATTR_PDF, pdf) ||
	    nla_put(skb, NHI_ATTR_MSG_FROM_ICM, msg_len, msg)) {
		res = -EMSGSIZE;
		goto nla_put_failure;
	}

	genlmsg_end(skb, msg_head);

	return genlmsg_unicast(&init_net, skb, port_id);

nla_put_failure:
	genlmsg_cancel(skb, msg_head);
genl_put_reply_failure:
	nlmsg_free(skb);

	return res;
}

static bool nhi_handle_inter_domain_msg(struct tbt_nhi_ctxt *nhi_ctxt,
					struct thunderbolt_ip_header *hdr)
{
	struct port_net_dev *port;
	u8 port_num;

	const uuid_be proto_uuid = APPLE_THUNDERBOLT_IP_PROTOCOL_UUID;

	if (uuid_be_cmp(proto_uuid, hdr->apple_tbt_ip_proto_uuid) != 0)
		return true;

	port_num = PORT_NUM_FROM_LINK(
				L0_PORT_NUM(be32_to_cpu(hdr->route_str.lo)));

	if (unlikely(port_num >= nhi_ctxt->num_ports))
		return false;

	port = &(nhi_ctxt->net_devices[port_num]);
	mutex_lock(&port->state_mutex);
	if (port->net_dev != NULL)
		negotiation_messages(port->net_dev, hdr);
	mutex_unlock(&port->state_mutex);

	return false;
}

static void nhi_handle_notification_msg(struct tbt_nhi_ctxt *nhi_ctxt,
					const u8 *msg)
{
	struct port_net_dev *port;
	u8 port_num;

	switch (msg[3]) {

	case NC_INTER_DOMAIN_CONNECTED:
		port_num = PORT_NUM_FROM_MSG(msg[5]);
#define INTER_DOMAIN_APPROVED BIT(3)
		if (port_num < nhi_ctxt->num_ports &&
		    !(msg[5] & INTER_DOMAIN_APPROVED))
			nhi_ctxt->net_devices[port_num].medium_sts =
						MEDIUM_READY_FOR_APPROVAL;
		break;

	case NC_INTER_DOMAIN_DISCONNECTED:
		port_num = PORT_NUM_FROM_MSG(msg[5]);

		if (unlikely(port_num >= nhi_ctxt->num_ports))
			break;

		port = &(nhi_ctxt->net_devices[port_num]);
		mutex_lock(&port->state_mutex);
		port->medium_sts = MEDIUM_DISCONNECTED;

		if (port->net_dev != NULL)
			negotiation_events(port->net_dev,
					   MEDIUM_DISCONNECTED);
		mutex_unlock(&port->state_mutex);
		break;
	}
}

static bool nhi_handle_icm_response_msg(struct tbt_nhi_ctxt *nhi_ctxt,
					const u8 *msg)
{
	struct port_net_dev *port;
	bool send_event = true;
	u8 port_num;

	if (nhi_ctxt->ignore_icm_resp &&
	    msg[3] == RC_INTER_DOMAIN_PKT_SENT) {
		nhi_ctxt->ignore_icm_resp = false;
		send_event = false;
	}
	if (nhi_ctxt->wait_for_icm_resp) {
		nhi_ctxt->wait_for_icm_resp = false;
		up(&nhi_ctxt->send_sem);
	}

	if (msg[3] == RC_APPROVE_INTER_DOMAIN_CONNECTION) {
#define APPROVE_INTER_DOMAIN_ERROR BIT(0)
		if (unlikely(msg[2] & APPROVE_INTER_DOMAIN_ERROR))
			return send_event;

		port_num = PORT_NUM_FROM_MSG(msg[5]);

		if (unlikely(port_num >= nhi_ctxt->num_ports))
			return send_event;

		port = &(nhi_ctxt->net_devices[port_num]);
		mutex_lock(&port->state_mutex);
		port->medium_sts = MEDIUM_CONNECTED;

		if (port->net_dev != NULL)
			negotiation_events(port->net_dev, MEDIUM_CONNECTED);
		mutex_unlock(&port->state_mutex);
	}

	return send_event;
}

static bool nhi_msg_from_icm_analysis(struct tbt_nhi_ctxt *nhi_ctxt,
					enum pdf_value pdf,
					const u8 *msg, u32 msg_len)
{
	bool send_event = true;

	switch (pdf) {
	case PDF_INTER_DOMAIN_REQUEST:
	case PDF_INTER_DOMAIN_RESPONSE:
		send_event = nhi_handle_inter_domain_msg(
					nhi_ctxt,
					(struct thunderbolt_ip_header *)msg);
		break;

	case PDF_FW_TO_SW_NOTIFICATION:
		nhi_handle_notification_msg(nhi_ctxt, msg);
		break;

	case PDF_ERROR_NOTIFICATION:
		/* fallthrough */
	case PDF_WRITE_CONFIGURATION_REGISTERS:
		/* fallthrough */
	case PDF_READ_CONFIGURATION_REGISTERS:
		if (nhi_ctxt->wait_for_icm_resp) {
			nhi_ctxt->wait_for_icm_resp = false;
			up(&nhi_ctxt->send_sem);
		}
		break;

	case PDF_FW_TO_SW_RESPONSE:
		send_event = nhi_handle_icm_response_msg(nhi_ctxt, msg);
		break;

	default:
		break;
	}

	return send_event;
}

static void nhi_msgs_from_icm(struct work_struct *work)
			      __releases(&nhi_ctxt->send_sem)
{
	struct tbt_nhi_ctxt *nhi_ctxt = container_of(work, typeof(*nhi_ctxt),
						     icm_msgs_work);
	void __iomem *reg = TBT_RING_CONS_PROD_REG(nhi_ctxt->iobase,
						   REG_RX_RING_BASE,
						   TBT_ICM_RING_NUM);
	u32 prod_cons, prod, cons;

	prod_cons = ioread32(reg);
	prod = TBT_REG_RING_PROD_EXTRACT(prod_cons);
	cons = TBT_REG_RING_CONS_EXTRACT(prod_cons);
	if (prod >= TBT_ICM_RING_NUM_RX_BUFS) {
		dev_warn(&nhi_ctxt->pdev->dev,
			 "controller id %#x is not functional, producer %u out of range\n",
			 nhi_ctxt->id, prod);
		return;
	}
	if (cons >= TBT_ICM_RING_NUM_RX_BUFS) {
		dev_warn(&nhi_ctxt->pdev->dev,
			 "controller id %#x is not functional, consumer %u out of range\n",
			 nhi_ctxt->id, cons);
		return;
	}

	while (!TBT_RX_RING_EMPTY(prod, cons, TBT_ICM_RING_NUM_RX_BUFS) &&
	       !nhi_ctxt->d0_exit) {
		struct tbt_buf_desc *rx_desc;
		u8 *msg;
		u32 msg_len;
		enum pdf_value pdf;
		bool send_event;

		cons = (cons + 1) % TBT_ICM_RING_NUM_RX_BUFS;
		rx_desc = &(nhi_ctxt->icm_ring_shared_mem->rx_buf_desc[cons]);
		if (!(le32_to_cpu(rx_desc->attributes) & DESC_ATTR_DESC_DONE))
			usleep_range(10, 20);

		rmb(); /* read the descriptor and the buffer after DD check */
		pdf = (le32_to_cpu(rx_desc->attributes) & DESC_ATTR_EOF_MASK)
		      >> DESC_ATTR_EOF_SHIFT;
		msg = nhi_ctxt->icm_ring_shared_mem->rx_buf[cons];
		msg_len = (le32_to_cpu(rx_desc->attributes)&DESC_ATTR_LEN_MASK)
			  >> DESC_ATTR_LEN_SHIFT;

		send_event = nhi_msg_from_icm_analysis(nhi_ctxt, pdf, msg,
						       msg_len);

		if (send_event)
			nhi_genl_send_msg(nhi_ctxt, pdf, msg, msg_len);

		/* set the descriptor for another receive */
		rx_desc->attributes = cpu_to_le32(DESC_ATTR_REQ_STS |
						  DESC_ATTR_INT_EN);
		rx_desc->time = 0;
	}

	/* free the descriptors for more receive */
	prod_cons &= ~REG_RING_CONS_MASK;
	prod_cons |= (cons << REG_RING_CONS_SHIFT) & REG_RING_CONS_MASK;
	iowrite32(prod_cons, reg);

	if (!nhi_ctxt->d0_exit) {
		unsigned long flags;

		spin_lock_irqsave(&nhi_ctxt->lock, flags);
		/* enable RX interrupt */
		RING_INT_ENABLE_RX(nhi_ctxt->iobase, TBT_ICM_RING_NUM,
				   nhi_ctxt->num_paths);

		spin_unlock_irqrestore(&nhi_ctxt->lock, flags);
	}
}

static irqreturn_t nhi_icm_ring_rx_msix(int __always_unused irq, void *data)
{
	struct tbt_nhi_ctxt *nhi_ctxt = data;

	spin_lock(&nhi_ctxt->lock);
	/*
	 * disable RX interrupt
	 * We like to allow interrupt mitigation until the work item
	 * will be completed.
	 */
	RING_INT_DISABLE_RX(nhi_ctxt->iobase, TBT_ICM_RING_NUM,
			    nhi_ctxt->num_paths);

	spin_unlock(&nhi_ctxt->lock);

	schedule_work(&nhi_ctxt->icm_msgs_work);

	return IRQ_HANDLED;
}

static irqreturn_t nhi_msi(int __always_unused irq, void *data)
{
	struct tbt_nhi_ctxt *nhi_ctxt = data;
	u32 isr0, isr1, imr0, imr1;
	int i;

	/* clear on read */
	isr0 = ioread32(nhi_ctxt->iobase + REG_RING_NOTIFY_BASE);
	isr1 = ioread32(nhi_ctxt->iobase + REG_RING_NOTIFY_BASE +
							REG_RING_NOTIFY_STEP);
	if (unlikely(!isr0 && !isr1))
		return IRQ_NONE;

	spin_lock(&nhi_ctxt->lock);

	imr0 = ioread32(nhi_ctxt->iobase + REG_RING_INTERRUPT_BASE);
	imr1 = ioread32(nhi_ctxt->iobase + REG_RING_INTERRUPT_BASE +
			REG_RING_INTERRUPT_STEP);
	/* disable the arrived interrupts */
	iowrite32(imr0 & ~isr0,
		  nhi_ctxt->iobase + REG_RING_INTERRUPT_BASE);
	iowrite32(imr1 & ~isr1,
		  nhi_ctxt->iobase + REG_RING_INTERRUPT_BASE +
		  REG_RING_INTERRUPT_STEP);

	spin_unlock(&nhi_ctxt->lock);

	for (i = 0; i < nhi_ctxt->num_ports; ++i) {
		struct net_device *net_dev =
				nhi_ctxt->net_devices[i].net_dev;
		if (net_dev) {
			u8 path = PATH_FROM_PORT(nhi_ctxt->num_paths, i);

			if (isr0 & REG_RING_INT_RX_PROCESSED(
					path, nhi_ctxt->num_paths))
				tbt_net_rx_msi(net_dev);
			if (isr0 & REG_RING_INT_TX_PROCESSED(path))
				tbt_net_tx_msi(net_dev);
		}
	}

	if (isr0 & REG_RING_INT_RX_PROCESSED(TBT_ICM_RING_NUM,
					     nhi_ctxt->num_paths))
		schedule_work(&nhi_ctxt->icm_msgs_work);

	return IRQ_HANDLED;
}

/**
 * nhi_set_int_vec - Mapping of the MSIX vector entry to the ring
 * @nhi_ctxt: contains data on NHI controller
 * @path: ring to be mapped
 * @msix_msg_id: msix entry to be mapped
 */
static inline void nhi_set_int_vec(struct tbt_nhi_ctxt *nhi_ctxt, u32 path,
				   u8 msix_msg_id)
{
	void __iomem *reg;
	u32 step, shift, ivr;

	if (msix_msg_id % 2)
		path += nhi_ctxt->num_paths;

	step = path / REG_INT_VEC_ALLOC_PER_REG;
	shift = (path % REG_INT_VEC_ALLOC_PER_REG) *
		REG_INT_VEC_ALLOC_FIELD_BITS;
	reg = nhi_ctxt->iobase + REG_INT_VEC_ALLOC_BASE +
					(step * REG_INT_VEC_ALLOC_STEP);
	ivr = ioread32(reg) & ~(REG_INT_VEC_ALLOC_FIELD_MASK << shift);
	iowrite32(ivr | (msix_msg_id << shift), reg);
}

/* NHI genetlink policy */
static const struct nla_policy nhi_genl_policy[NHI_ATTR_MAX + 1] = {
	[NHI_ATTR_DRV_VERSION]		= { .type = NLA_NUL_STRING, },
	[NHI_ATTR_NVM_VER_OFFSET]	= { .type = NLA_U16, },
	[NHI_ATTR_NUM_PORTS]		= { .type = NLA_U8, },
	[NHI_ATTR_DMA_PORT]		= { .type = NLA_U8, },
	[NHI_ATTR_SUPPORT_FULL_E2E]	= { .type = NLA_FLAG, },
	[NHI_ATTR_MAILBOX_CMD]		= { .type = NLA_U32, },
	[NHI_ATTR_PDF]			= { .type = NLA_U32, },
	[NHI_ATTR_MSG_TO_ICM]		= { .type = NLA_BINARY,
					.len = TBT_ICM_RING_MAX_FRAME_SIZE },
	[NHI_ATTR_MSG_FROM_ICM]		= { .type = NLA_BINARY,
					.len = TBT_ICM_RING_MAX_FRAME_SIZE },
	[NHI_ATTR_LOCAL_ROUTE_STRING]	= {
					.len = sizeof(struct route_string) },
	[NHI_ATTR_LOCAL_UUID]		= { .len = sizeof(uuid_be) },
	[NHI_ATTR_REMOTE_UUID]		= { .len = sizeof(uuid_be) },
	[NHI_ATTR_LOCAL_DEPTH]		= { .type = NLA_U8, },
	[NHI_ATTR_ENABLE_FULL_E2E]	= { .type = NLA_FLAG, },
	[NHI_ATTR_MATCH_FRAME_ID]	= { .type = NLA_FLAG, },
};

/* NHI genetlink operations array */
static const struct genl_ops nhi_ops[] = {
	{
		.cmd = NHI_CMD_SUBSCRIBE,
		.policy = nhi_genl_policy,
		.doit = nhi_genl_subscribe,
	},
	{
		.cmd = NHI_CMD_UNSUBSCRIBE,
		.policy = nhi_genl_policy,
		.doit = nhi_genl_unsubscribe,
	},
	{
		.cmd = NHI_CMD_QUERY_INFORMATION,
		.policy = nhi_genl_policy,
		.doit = nhi_genl_query_information,
	},
	{
		.cmd = NHI_CMD_MSG_TO_ICM,
		.policy = nhi_genl_policy,
		.doit = nhi_genl_msg_to_icm,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NHI_CMD_MAILBOX,
		.policy = nhi_genl_policy,
		.doit = nhi_genl_mailbox,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NHI_CMD_APPROVE_TBT_NETWORKING,
		.policy = nhi_genl_policy,
		.doit = nhi_genl_approve_networking,
		.flags = GENL_ADMIN_PERM,
	},
};

/* NHI genetlink family */
static struct genl_family nhi_genl_family __ro_after_init = {
	.hdrsize	= FIELD_SIZEOF(struct tbt_nhi_ctxt, id),
	.name		= NHI_GENL_NAME,
	.version	= NHI_GENL_VERSION,
	.maxattr	= NHI_ATTR_MAX,
	.ops		= nhi_ops,
	.n_ops		= ARRAY_SIZE(nhi_ops),
};

static int nhi_suspend(struct device *dev) __releases(&nhi_ctxt->send_sem)
{
	struct tbt_nhi_ctxt *nhi_ctxt = pci_get_drvdata(to_pci_dev(dev));
	void __iomem *rx_reg, *tx_reg;
	u32 rx_reg_val, tx_reg_val;
	int i;

	for (i = 0; i < nhi_ctxt->num_ports; i++) {
		struct port_net_dev *port = &nhi_ctxt->net_devices[i];

		mutex_lock(&port->state_mutex);
		port->medium_sts = MEDIUM_DISCONNECTED;
		if (port->net_dev)
			negotiation_events(port->net_dev, MEDIUM_DISCONNECTED);
		mutex_unlock(&port->state_mutex);
	}

	/* must be after negotiation_events, since messages might be sent */
	nhi_ctxt->d0_exit = true;

	rx_reg = nhi_ctxt->iobase + REG_RX_OPTIONS_BASE +
		 (TBT_ICM_RING_NUM * REG_OPTS_STEP);
	rx_reg_val = ioread32(rx_reg) & ~REG_OPTS_E2E_EN;
	tx_reg = nhi_ctxt->iobase + REG_TX_OPTIONS_BASE +
		 (TBT_ICM_RING_NUM * REG_OPTS_STEP);
	tx_reg_val = ioread32(tx_reg) & ~REG_OPTS_E2E_EN;
	/* disable RX flow control  */
	iowrite32(rx_reg_val, rx_reg);
	/* disable TX flow control  */
	iowrite32(tx_reg_val, tx_reg);
	/* disable RX ring  */
	iowrite32(rx_reg_val & ~REG_OPTS_VALID, rx_reg);

	mutex_lock(&nhi_ctxt->d0_exit_mailbox_mutex);
	mutex_lock(&nhi_ctxt->d0_exit_send_mutex);

	cancel_work_sync(&nhi_ctxt->icm_msgs_work);

	if (nhi_ctxt->wait_for_icm_resp) {
		nhi_ctxt->wait_for_icm_resp = false;
		nhi_ctxt->ignore_icm_resp = false;
		/*
		 * if there is response, it is lost, so unlock the send
		 * for the next resume.
		 */
		up(&nhi_ctxt->send_sem);
	}

	mutex_unlock(&nhi_ctxt->d0_exit_send_mutex);
	mutex_unlock(&nhi_ctxt->d0_exit_mailbox_mutex);

	/* wait for all TX to finish  */
	usleep_range(5 * USEC_PER_MSEC, 7 * USEC_PER_MSEC);

	/* disable all interrupts */
	iowrite32(0, nhi_ctxt->iobase + REG_RING_INTERRUPT_BASE);
	/* disable TX ring  */
	iowrite32(tx_reg_val & ~REG_OPTS_VALID, tx_reg);

	return 0;
}

static int nhi_resume(struct device *dev) __acquires(&nhi_ctxt->send_sem)
{
	dma_addr_t phys;
	struct tbt_nhi_ctxt *nhi_ctxt = pci_get_drvdata(to_pci_dev(dev));
	struct tbt_buf_desc *desc;
	void __iomem *iobase = nhi_ctxt->iobase;
	void __iomem *reg;
	int i;

	if (nhi_ctxt->msix_entries) {
		iowrite32(ioread32(iobase + REG_DMA_MISC) |
						REG_DMA_MISC_INT_AUTO_CLEAR,
			  iobase + REG_DMA_MISC);
		/*
		 * Vector #0, which is TX complete to ICM,
		 * isn't been used currently.
		 */
		nhi_set_int_vec(nhi_ctxt, 0, 1);

		for (i = 2; i < nhi_ctxt->num_vectors; i++)
			nhi_set_int_vec(nhi_ctxt, nhi_ctxt->num_paths - (i/2),
					i);
	}

	/* configure TX descriptors */
	for (i = 0, phys = nhi_ctxt->icm_ring_shared_mem_dma_addr;
	     i < TBT_ICM_RING_NUM_TX_BUFS;
	     i++, phys += TBT_ICM_RING_MAX_FRAME_SIZE) {
		desc = &nhi_ctxt->icm_ring_shared_mem->tx_buf_desc[i];
		desc->phys = cpu_to_le64(phys);
		desc->attributes = cpu_to_le32(DESC_ATTR_REQ_STS);
	}
	/* configure RX descriptors */
	for (i = 0;
	     i < TBT_ICM_RING_NUM_RX_BUFS;
	     i++, phys += TBT_ICM_RING_MAX_FRAME_SIZE) {
		desc = &nhi_ctxt->icm_ring_shared_mem->rx_buf_desc[i];
		desc->phys = cpu_to_le64(phys);
		desc->attributes = cpu_to_le32(DESC_ATTR_REQ_STS |
					       DESC_ATTR_INT_EN);
	}

	/* configure throttling rate for interrupts */
	for (i = 0, reg = iobase + REG_INT_THROTTLING_RATE;
	     i < NUM_INT_VECTORS;
	     i++, reg += REG_INT_THROTTLING_RATE_STEP) {
		iowrite32(USEC_TO_256_NSECS(128), reg);
	}

	/* configure TX for ICM ring */
	reg = iobase + REG_TX_RING_BASE + (TBT_ICM_RING_NUM * REG_RING_STEP);
	phys = nhi_ctxt->icm_ring_shared_mem_dma_addr +
		offsetof(struct tbt_icm_ring_shared_memory, tx_buf_desc);
	iowrite32(lower_32_bits(phys), reg + REG_RING_PHYS_LO_OFFSET);
	iowrite32(upper_32_bits(phys), reg + REG_RING_PHYS_HI_OFFSET);
	iowrite32((TBT_ICM_RING_NUM_TX_BUFS << REG_RING_SIZE_SHIFT) &
			REG_RING_SIZE_MASK,
		  reg + REG_RING_SIZE_OFFSET);

	reg = iobase + REG_TX_OPTIONS_BASE + (TBT_ICM_RING_NUM*REG_OPTS_STEP);
	iowrite32(REG_OPTS_RAW | REG_OPTS_VALID, reg);

	/* configure RX for ICM ring */
	reg = iobase + REG_RX_RING_BASE + (TBT_ICM_RING_NUM * REG_RING_STEP);
	phys = nhi_ctxt->icm_ring_shared_mem_dma_addr +
		offsetof(struct tbt_icm_ring_shared_memory, rx_buf_desc);
	iowrite32(lower_32_bits(phys), reg + REG_RING_PHYS_LO_OFFSET);
	iowrite32(upper_32_bits(phys), reg + REG_RING_PHYS_HI_OFFSET);
	iowrite32(((TBT_ICM_RING_NUM_RX_BUFS << REG_RING_SIZE_SHIFT) &
			REG_RING_SIZE_MASK) |
		  ((TBT_ICM_RING_MAX_FRAME_SIZE << REG_RING_BUF_SIZE_SHIFT) &
			REG_RING_BUF_SIZE_MASK),
		  reg + REG_RING_SIZE_OFFSET);
	iowrite32(((TBT_ICM_RING_NUM_RX_BUFS - 1) << REG_RING_CONS_SHIFT) &
			REG_RING_CONS_MASK,
		  reg + REG_RING_CONS_PROD_OFFSET);

	reg = iobase + REG_RX_OPTIONS_BASE + (TBT_ICM_RING_NUM*REG_OPTS_STEP);
	iowrite32(REG_OPTS_RAW | REG_OPTS_VALID, reg);

	/* enable RX interrupt */
	RING_INT_ENABLE_RX(iobase, TBT_ICM_RING_NUM, nhi_ctxt->num_paths);

	if (likely((atomic_read(&subscribers) > 0) &&
		   nhi_nvm_authenticated(nhi_ctxt))) {
		down(&nhi_ctxt->send_sem);
		nhi_ctxt->d0_exit = false;
		mutex_lock(&nhi_ctxt->d0_exit_send_mutex);
		/*
		 * interrupts are enabled here before send due to
		 * implicit barrier in mutex
		 */
		nhi_send_driver_ready_command(nhi_ctxt);
		mutex_unlock(&nhi_ctxt->d0_exit_send_mutex);
	} else {
		nhi_ctxt->d0_exit = false;
	}

	return 0;
}

static void icm_nhi_shutdown(struct pci_dev *pdev)
{
	nhi_suspend(&pdev->dev);
}

static void icm_nhi_remove(struct pci_dev *pdev)
{
	struct tbt_nhi_ctxt *nhi_ctxt = pci_get_drvdata(pdev);
	int i;

	nhi_suspend(&pdev->dev);

	for (i = 0; i < nhi_ctxt->num_ports; i++) {
		mutex_lock(&nhi_ctxt->net_devices[i].state_mutex);
		if (nhi_ctxt->net_devices[i].net_dev) {
			nhi_dealloc_etherdev(nhi_ctxt->net_devices[i].net_dev);
			nhi_ctxt->net_devices[i].net_dev = NULL;
		}
		mutex_unlock(&nhi_ctxt->net_devices[i].state_mutex);
	}

	if (nhi_ctxt->net_workqueue)
		destroy_workqueue(nhi_ctxt->net_workqueue);

	/*
	 * disable irq for msix or msi
	 */
	if (likely(nhi_ctxt->msix_entries)) {
		/* Vector #0 isn't been used currently */
		devm_free_irq(&pdev->dev, nhi_ctxt->msix_entries[1].vector,
			      nhi_ctxt);
		pci_disable_msix(pdev);
	} else {
		devm_free_irq(&pdev->dev, pdev->irq, nhi_ctxt);
		pci_disable_msi(pdev);
	}

	/*
	 * remove controller from the controllers list
	 */
	mutex_lock(&controllers_list_mutex);
	list_del(&nhi_ctxt->node);
	mutex_unlock(&controllers_list_mutex);

	nhi_mailbox(
		nhi_ctxt,
		((CC_DRV_UNLOADS_AND_DISCONNECT_INTER_DOMAIN_PATHS
		  << REG_INMAIL_CMD_CMD_SHIFT) &
		 REG_INMAIL_CMD_CMD_MASK) |
		REG_INMAIL_CMD_REQUEST,
		0, true);

	usleep_range(1 * USEC_PER_MSEC, 5 * USEC_PER_MSEC);
	iowrite32(1, nhi_ctxt->iobase + REG_HOST_INTERFACE_RST);

	mutex_destroy(&nhi_ctxt->d0_exit_send_mutex);
	mutex_destroy(&nhi_ctxt->d0_exit_mailbox_mutex);
	mutex_destroy(&nhi_ctxt->mailbox_mutex);
	for (i = 0; i < nhi_ctxt->num_ports; i++)
		mutex_destroy(&(nhi_ctxt->net_devices[i].state_mutex));
}

static int icm_nhi_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct tbt_nhi_ctxt *nhi_ctxt;
	void __iomem *iobase;
	int i, res;
	bool enable_msi = false;

	res = pcim_enable_device(pdev);
	if (res) {
		dev_err(&pdev->dev, "cannot enable PCI device, aborting\n");
		return res;
	}

	res = pcim_iomap_regions(pdev, 1 << NHI_MMIO_BAR, pci_name(pdev));
	if (res) {
		dev_err(&pdev->dev, "cannot obtain PCI resources, aborting\n");
		return res;
	}

	/* cannot fail - table is allocated in pcim_iomap_regions */
	iobase = pcim_iomap_table(pdev)[NHI_MMIO_BAR];

	/* check if ICM is running */
	if (!(ioread32(iobase + REG_FW_STS) & REG_FW_STS_ICM_EN)) {
		dev_err(&pdev->dev, "ICM isn't present, aborting\n");
		return -ENODEV;
	}

	nhi_ctxt = devm_kzalloc(&pdev->dev, sizeof(*nhi_ctxt), GFP_KERNEL);
	if (!nhi_ctxt)
		return -ENOMEM;

	nhi_ctxt->pdev = pdev;
	nhi_ctxt->iobase = iobase;
	nhi_ctxt->id = (PCI_DEVID(pdev->bus->number, pdev->devfn) << 16) |
								id->device;
	/*
	 * Number of paths represents the number of rings available for
	 * the controller.
	 */
	nhi_ctxt->num_paths = ioread32(iobase + REG_HOP_COUNT) &
						REG_HOP_COUNT_TOTAL_PATHS_MASK;

	nhi_ctxt->nvm_auth_on_boot = DEVICE_DATA_NVM_AUTH_ON_BOOT(
							id->driver_data);
	nhi_ctxt->support_full_e2e = DEVICE_DATA_SUPPORT_FULL_E2E(
							id->driver_data);

	nhi_ctxt->dma_port = DEVICE_DATA_DMA_PORT(id->driver_data);
	/*
	 * Number of ports in the controller
	 */
	nhi_ctxt->num_ports = DEVICE_DATA_NUM_PORTS(id->driver_data);
	nhi_ctxt->nvm_ver_offset = DEVICE_DATA_NVM_VER_OFFSET(id->driver_data);

	mutex_init(&nhi_ctxt->d0_exit_send_mutex);
	mutex_init(&nhi_ctxt->d0_exit_mailbox_mutex);
	mutex_init(&nhi_ctxt->mailbox_mutex);

	sema_init(&nhi_ctxt->send_sem, 1);

	INIT_WORK(&nhi_ctxt->icm_msgs_work, nhi_msgs_from_icm);

	spin_lock_init(&nhi_ctxt->lock);

	nhi_ctxt->net_devices = devm_kcalloc(&pdev->dev,
					     nhi_ctxt->num_ports,
					     sizeof(struct port_net_dev),
					     GFP_KERNEL);
	if (!nhi_ctxt->net_devices)
		return -ENOMEM;

	for (i = 0; i < nhi_ctxt->num_ports; i++)
		mutex_init(&(nhi_ctxt->net_devices[i].state_mutex));

	/*
	 * allocating RX and TX vectors for ICM and per port
	 * for thunderbolt networking.
	 * The mapping of the vector is carried out by
	 * nhi_set_int_vec and looks like:
	 * 0=tx icm, 1=rx icm, 2=tx data port 0,
	 * 3=rx data port 0...
	 */
	nhi_ctxt->num_vectors = (1 + nhi_ctxt->num_ports) * 2;
	nhi_ctxt->msix_entries = devm_kcalloc(&pdev->dev,
					      nhi_ctxt->num_vectors,
					      sizeof(struct msix_entry),
					      GFP_KERNEL);
	if (likely(nhi_ctxt->msix_entries)) {
		for (i = 0; i < nhi_ctxt->num_vectors; i++)
			nhi_ctxt->msix_entries[i].entry = i;
		res = pci_enable_msix_exact(pdev,
					    nhi_ctxt->msix_entries,
					    nhi_ctxt->num_vectors);

		if (res ||
		    /*
		     * Allocating ICM RX only.
		     * vector #0, which is TX complete to ICM,
		     * isn't been used currently
		     */
		    devm_request_irq(&pdev->dev,
				     nhi_ctxt->msix_entries[1].vector,
				     nhi_icm_ring_rx_msix, 0, pci_name(pdev),
				     nhi_ctxt)) {
			devm_kfree(&pdev->dev, nhi_ctxt->msix_entries);
			nhi_ctxt->msix_entries = NULL;
			enable_msi = true;
		}
	} else {
		enable_msi = true;
	}
	/*
	 * In case allocation didn't succeed, use msi instead of msix
	 */
	if (enable_msi) {
		res = pci_enable_msi(pdev);
		if (res) {
			dev_err(&pdev->dev, "cannot enable MSI, aborting\n");
			return res;
		}
		res = devm_request_irq(&pdev->dev, pdev->irq, nhi_msi, 0,
				       pci_name(pdev), nhi_ctxt);
		if (res) {
			dev_err(&pdev->dev,
				"request_irq failed %d, aborting\n", res);
			return res;
		}
	}
	/*
	 * try to work with address space of 64 bits.
	 * In case this doesn't work, work with 32 bits.
	 */
	if (!dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		nhi_ctxt->pci_using_dac = true;
	} else {
		res = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (res) {
			dev_err(&pdev->dev,
				"No suitable DMA available, aborting\n");
			return res;
		}
	}

	BUILD_BUG_ON(sizeof(struct tbt_buf_desc) != 16);
	BUILD_BUG_ON(sizeof(struct tbt_icm_ring_shared_memory) > PAGE_SIZE);
	nhi_ctxt->icm_ring_shared_mem = dmam_alloc_coherent(
			&pdev->dev, sizeof(*nhi_ctxt->icm_ring_shared_mem),
			&nhi_ctxt->icm_ring_shared_mem_dma_addr,
			GFP_KERNEL | __GFP_ZERO);
	if (nhi_ctxt->icm_ring_shared_mem == NULL) {
		dev_err(&pdev->dev, "dmam_alloc_coherent failed, aborting\n");
		return -ENOMEM;
	}

	nhi_ctxt->net_workqueue = create_singlethread_workqueue("thunderbolt");
	if (!nhi_ctxt->net_workqueue) {
		dev_err(&pdev->dev, "create_singlethread_workqueue failed, aborting\n");
		return -ENOMEM;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, nhi_ctxt);

	nhi_resume(&pdev->dev);
	/*
	 * Add the new controller at the end of the list
	 */
	mutex_lock(&controllers_list_mutex);
	list_add_tail(&nhi_ctxt->node, &controllers_list);
	mutex_unlock(&controllers_list_mutex);

	return res;
}

/*
 * The tunneled pci bridges are siblings of us. Use resume_noirq to reenable
 * the tunnels asap. A corresponding pci quirk blocks the downstream bridges
 * resume_noirq until we are done.
 */
static const struct dev_pm_ops icm_nhi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(nhi_suspend, nhi_resume)
};

static const struct pci_device_id nhi_pci_device_ids[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_REDWOOD_RIDGE_2C_NHI),
					DEVICE_DATA(1, 5, 0xa, false, false) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_REDWOOD_RIDGE_4C_NHI),
					DEVICE_DATA(2, 5, 0xa, false, false) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_NHI),
					DEVICE_DATA(1, 5, 0xa, false, false) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_NHI),
					DEVICE_DATA(2, 5, 0xa, false, false) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_WIN_RIDGE_2C_NHI),
					DEVICE_DATA(1, 3, 0xa, false, false) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_NHI),
					DEVICE_DATA(1, 5, 0xa, true, true) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_NHI),
					DEVICE_DATA(2, 5, 0xa, true, true) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_USBONLY_NHI),
					DEVICE_DATA(1, 5, 0xa, true, true) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_NHI),
					DEVICE_DATA(1, 3, 0xa, true, true) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_USBONLY_NHI),
					DEVICE_DATA(1, 3, 0xa, true, true) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_NHI),
					DEVICE_DATA(1, 5, 0xa, true, true) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_NHI),
					DEVICE_DATA(2, 5, 0xa, true, true) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_USBONLY_NHI),
					DEVICE_DATA(1, 5, 0xa, true, true) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, nhi_pci_device_ids);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static struct pci_driver icm_nhi_driver = {
	.name = "thunderbolt",
	.id_table = nhi_pci_device_ids,
	.probe = icm_nhi_probe,
	.remove = icm_nhi_remove,
	.shutdown = icm_nhi_shutdown,
	.driver.pm = &icm_nhi_pm_ops,
};

static int __init icm_nhi_init(void)
{
	int rc;

	if (dmi_match(DMI_BOARD_VENDOR, "Apple Inc."))
		return -ENODEV;

	rc = genl_register_family(&nhi_genl_family);
	if (rc)
		goto failure;

	rc = pci_register_driver(&icm_nhi_driver);
	if (rc)
		goto failure_genl;

	return 0;

failure_genl:
	genl_unregister_family(&nhi_genl_family);

failure:
	pr_debug("nhi: error %d occurred in %s\n", rc, __func__);
	return rc;
}

static void __exit icm_nhi_unload(void)
{
	genl_unregister_family(&nhi_genl_family);
	pci_unregister_driver(&icm_nhi_driver);
}

module_init(icm_nhi_init);
module_exit(icm_nhi_unload);
