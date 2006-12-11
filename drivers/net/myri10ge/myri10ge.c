/*************************************************************************
 * myri10ge.c: Myricom Myri-10G Ethernet driver.
 *
 * Copyright (C) 2005, 2006 Myricom, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Myricom, Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * If the eeprom on your board is not recent enough, you will need to get a
 * newer firmware image at:
 *   http://www.myri.com/scs/download-Myri10GE.html
 *
 * Contact Information:
 *   <help@myri.com>
 *   Myricom, Inc., 325N Santa Anita Avenue, Arcadia, CA 91006
 *************************************************************************/

#include <linux/tcp.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/ethtool.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/crc32.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <net/checksum.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/processor.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include "myri10ge_mcp.h"
#include "myri10ge_mcp_gen_header.h"

#define MYRI10GE_VERSION_STR "1.0.0"

MODULE_DESCRIPTION("Myricom 10G driver (10GbE)");
MODULE_AUTHOR("Maintainer: help@myri.com");
MODULE_VERSION(MYRI10GE_VERSION_STR);
MODULE_LICENSE("Dual BSD/GPL");

#define MYRI10GE_MAX_ETHER_MTU 9014

#define MYRI10GE_ETH_STOPPED 0
#define MYRI10GE_ETH_STOPPING 1
#define MYRI10GE_ETH_STARTING 2
#define MYRI10GE_ETH_RUNNING 3
#define MYRI10GE_ETH_OPEN_FAILED 4

#define MYRI10GE_EEPROM_STRINGS_SIZE 256
#define MYRI10GE_MAX_SEND_DESC_TSO ((65536 / 2048) * 2)

#define MYRI10GE_NO_CONFIRM_DATA htonl(0xffffffff)
#define MYRI10GE_NO_RESPONSE_RESULT 0xffffffff

#define MYRI10GE_ALLOC_ORDER 0
#define MYRI10GE_ALLOC_SIZE ((1 << MYRI10GE_ALLOC_ORDER) * PAGE_SIZE)
#define MYRI10GE_MAX_FRAGS_PER_FRAME (MYRI10GE_MAX_ETHER_MTU/MYRI10GE_ALLOC_SIZE + 1)

struct myri10ge_rx_buffer_state {
	struct sk_buff *skb;
	struct page *page;
	int page_offset;
	 DECLARE_PCI_UNMAP_ADDR(bus)
	 DECLARE_PCI_UNMAP_LEN(len)
};

struct myri10ge_tx_buffer_state {
	struct sk_buff *skb;
	int last;
	 DECLARE_PCI_UNMAP_ADDR(bus)
	 DECLARE_PCI_UNMAP_LEN(len)
};

struct myri10ge_cmd {
	u32 data0;
	u32 data1;
	u32 data2;
};

struct myri10ge_rx_buf {
	struct mcp_kreq_ether_recv __iomem *lanai;	/* lanai ptr for recv ring */
	u8 __iomem *wc_fifo;	/* w/c rx dma addr fifo address */
	struct mcp_kreq_ether_recv *shadow;	/* host shadow of recv ring */
	struct myri10ge_rx_buffer_state *info;
	struct page *page;
	dma_addr_t bus;
	int page_offset;
	int cnt;
	int fill_cnt;
	int alloc_fail;
	int mask;		/* number of rx slots -1 */
	int watchdog_needed;
};

struct myri10ge_tx_buf {
	struct mcp_kreq_ether_send __iomem *lanai;	/* lanai ptr for sendq */
	u8 __iomem *wc_fifo;	/* w/c send fifo address */
	struct mcp_kreq_ether_send *req_list;	/* host shadow of sendq */
	char *req_bytes;
	struct myri10ge_tx_buffer_state *info;
	int mask;		/* number of transmit slots -1  */
	int boundary;		/* boundary transmits cannot cross */
	int req ____cacheline_aligned;	/* transmit slots submitted     */
	int pkt_start;		/* packets started */
	int done ____cacheline_aligned;	/* transmit slots completed     */
	int pkt_done;		/* packets completed */
};

struct myri10ge_rx_done {
	struct mcp_slot *entry;
	dma_addr_t bus;
	int cnt;
	int idx;
};

struct myri10ge_priv {
	int running;		/* running?             */
	int csum_flag;		/* rx_csums?            */
	struct myri10ge_tx_buf tx;	/* transmit ring        */
	struct myri10ge_rx_buf rx_small;
	struct myri10ge_rx_buf rx_big;
	struct myri10ge_rx_done rx_done;
	int small_bytes;
	int big_bytes;
	struct net_device *dev;
	struct net_device_stats stats;
	u8 __iomem *sram;
	int sram_size;
	unsigned long board_span;
	unsigned long iomem_base;
	__be32 __iomem *irq_claim;
	__be32 __iomem *irq_deassert;
	char *mac_addr_string;
	struct mcp_cmd_response *cmd;
	dma_addr_t cmd_bus;
	struct mcp_irq_data *fw_stats;
	dma_addr_t fw_stats_bus;
	struct pci_dev *pdev;
	int msi_enabled;
	__be32 link_state;
	unsigned int rdma_tags_available;
	int intr_coal_delay;
	__be32 __iomem *intr_coal_delay_ptr;
	int mtrr;
	int wake_queue;
	int stop_queue;
	int down_cnt;
	wait_queue_head_t down_wq;
	struct work_struct watchdog_work;
	struct timer_list watchdog_timer;
	int watchdog_tx_done;
	int watchdog_tx_req;
	int watchdog_resets;
	int tx_linearized;
	int pause;
	char *fw_name;
	char eeprom_strings[MYRI10GE_EEPROM_STRINGS_SIZE];
	char fw_version[128];
	u8 mac_addr[6];		/* eeprom mac address */
	unsigned long serial_number;
	int vendor_specific_offset;
	int fw_multicast_support;
	u32 devctl;
	u16 msi_flags;
	u32 read_dma;
	u32 write_dma;
	u32 read_write_dma;
	u32 link_changes;
	u32 msg_enable;
};

static char *myri10ge_fw_unaligned = "myri10ge_ethp_z8e.dat";
static char *myri10ge_fw_aligned = "myri10ge_eth_z8e.dat";

static char *myri10ge_fw_name = NULL;
module_param(myri10ge_fw_name, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(myri10ge_fw_name, "Firmware image name\n");

static int myri10ge_ecrc_enable = 1;
module_param(myri10ge_ecrc_enable, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_ecrc_enable, "Enable Extended CRC on PCI-E\n");

static int myri10ge_max_intr_slots = 1024;
module_param(myri10ge_max_intr_slots, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_max_intr_slots, "Interrupt queue slots\n");

static int myri10ge_small_bytes = -1;	/* -1 == auto */
module_param(myri10ge_small_bytes, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(myri10ge_small_bytes, "Threshold of small packets\n");

static int myri10ge_msi = 1;	/* enable msi by default */
module_param(myri10ge_msi, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_msi, "Enable Message Signalled Interrupts\n");

static int myri10ge_intr_coal_delay = 25;
module_param(myri10ge_intr_coal_delay, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_intr_coal_delay, "Interrupt coalescing delay\n");

static int myri10ge_flow_control = 1;
module_param(myri10ge_flow_control, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_flow_control, "Pause parameter\n");

static int myri10ge_deassert_wait = 1;
module_param(myri10ge_deassert_wait, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(myri10ge_deassert_wait,
		 "Wait when deasserting legacy interrupts\n");

static int myri10ge_force_firmware = 0;
module_param(myri10ge_force_firmware, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_force_firmware,
		 "Force firmware to assume aligned completions\n");

static int myri10ge_skb_cross_4k = 0;
module_param(myri10ge_skb_cross_4k, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(myri10ge_skb_cross_4k,
		 "Can a small skb cross a 4KB boundary?\n");

static int myri10ge_initial_mtu = MYRI10GE_MAX_ETHER_MTU - ETH_HLEN;
module_param(myri10ge_initial_mtu, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_initial_mtu, "Initial MTU\n");

static int myri10ge_napi_weight = 64;
module_param(myri10ge_napi_weight, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_napi_weight, "Set NAPI weight\n");

static int myri10ge_watchdog_timeout = 1;
module_param(myri10ge_watchdog_timeout, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_watchdog_timeout, "Set watchdog timeout\n");

static int myri10ge_max_irq_loops = 1048576;
module_param(myri10ge_max_irq_loops, int, S_IRUGO);
MODULE_PARM_DESC(myri10ge_max_irq_loops,
		 "Set stuck legacy IRQ detection threshold\n");

#define MYRI10GE_MSG_DEFAULT NETIF_MSG_LINK

static int myri10ge_debug = -1;	/* defaults above */
module_param(myri10ge_debug, int, 0);
MODULE_PARM_DESC(myri10ge_debug, "Debug level (0=none,...,16=all)");

static int myri10ge_fill_thresh = 256;
module_param(myri10ge_fill_thresh, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(myri10ge_fill_thresh, "Number of empty rx slots allowed\n");

#define MYRI10GE_FW_OFFSET 1024*1024
#define MYRI10GE_HIGHPART_TO_U32(X) \
(sizeof (X) == 8) ? ((u32)((u64)(X) >> 32)) : (0)
#define MYRI10GE_LOWPART_TO_U32(X) ((u32)(X))

#define myri10ge_pio_copy(to,from,size) __iowrite64_copy(to,from,size/8)

static inline void put_be32(__be32 val, __be32 __iomem * p)
{
	__raw_writel((__force __u32) val, (__force void __iomem *)p);
}

static int
myri10ge_send_cmd(struct myri10ge_priv *mgp, u32 cmd,
		  struct myri10ge_cmd *data, int atomic)
{
	struct mcp_cmd *buf;
	char buf_bytes[sizeof(*buf) + 8];
	struct mcp_cmd_response *response = mgp->cmd;
	char __iomem *cmd_addr = mgp->sram + MXGEFW_ETH_CMD;
	u32 dma_low, dma_high, result, value;
	int sleep_total = 0;

	/* ensure buf is aligned to 8 bytes */
	buf = (struct mcp_cmd *)ALIGN((unsigned long)buf_bytes, 8);

	buf->data0 = htonl(data->data0);
	buf->data1 = htonl(data->data1);
	buf->data2 = htonl(data->data2);
	buf->cmd = htonl(cmd);
	dma_low = MYRI10GE_LOWPART_TO_U32(mgp->cmd_bus);
	dma_high = MYRI10GE_HIGHPART_TO_U32(mgp->cmd_bus);

	buf->response_addr.low = htonl(dma_low);
	buf->response_addr.high = htonl(dma_high);
	response->result = htonl(MYRI10GE_NO_RESPONSE_RESULT);
	mb();
	myri10ge_pio_copy(cmd_addr, buf, sizeof(*buf));

	/* wait up to 15ms. Longest command is the DMA benchmark,
	 * which is capped at 5ms, but runs from a timeout handler
	 * that runs every 7.8ms. So a 15ms timeout leaves us with
	 * a 2.2ms margin
	 */
	if (atomic) {
		/* if atomic is set, do not sleep,
		 * and try to get the completion quickly
		 * (1ms will be enough for those commands) */
		for (sleep_total = 0;
		     sleep_total < 1000
		     && response->result == htonl(MYRI10GE_NO_RESPONSE_RESULT);
		     sleep_total += 10)
			udelay(10);
	} else {
		/* use msleep for most command */
		for (sleep_total = 0;
		     sleep_total < 15
		     && response->result == htonl(MYRI10GE_NO_RESPONSE_RESULT);
		     sleep_total++)
			msleep(1);
	}

	result = ntohl(response->result);
	value = ntohl(response->data);
	if (result != MYRI10GE_NO_RESPONSE_RESULT) {
		if (result == 0) {
			data->data0 = value;
			return 0;
		} else if (result == MXGEFW_CMD_UNKNOWN) {
			return -ENOSYS;
		} else {
			dev_err(&mgp->pdev->dev,
				"command %d failed, result = %d\n",
				cmd, result);
			return -ENXIO;
		}
	}

	dev_err(&mgp->pdev->dev, "command %d timed out, result = %d\n",
		cmd, result);
	return -EAGAIN;
}

/*
 * The eeprom strings on the lanaiX have the format
 * SN=x\0
 * MAC=x:x:x:x:x:x\0
 * PT:ddd mmm xx xx:xx:xx xx\0
 * PV:ddd mmm xx xx:xx:xx xx\0
 */
static int myri10ge_read_mac_addr(struct myri10ge_priv *mgp)
{
	char *ptr, *limit;
	int i;

	ptr = mgp->eeprom_strings;
	limit = mgp->eeprom_strings + MYRI10GE_EEPROM_STRINGS_SIZE;

	while (*ptr != '\0' && ptr < limit) {
		if (memcmp(ptr, "MAC=", 4) == 0) {
			ptr += 4;
			mgp->mac_addr_string = ptr;
			for (i = 0; i < 6; i++) {
				if ((ptr + 2) > limit)
					goto abort;
				mgp->mac_addr[i] =
				    simple_strtoul(ptr, &ptr, 16);
				ptr += 1;
			}
		}
		if (memcmp((const void *)ptr, "SN=", 3) == 0) {
			ptr += 3;
			mgp->serial_number = simple_strtoul(ptr, &ptr, 10);
		}
		while (ptr < limit && *ptr++) ;
	}

	return 0;

abort:
	dev_err(&mgp->pdev->dev, "failed to parse eeprom_strings\n");
	return -ENXIO;
}

/*
 * Enable or disable periodic RDMAs from the host to make certain
 * chipsets resend dropped PCIe messages
 */

static void myri10ge_dummy_rdma(struct myri10ge_priv *mgp, int enable)
{
	char __iomem *submit;
	__be32 buf[16];
	u32 dma_low, dma_high;
	int i;

	/* clear confirmation addr */
	mgp->cmd->data = 0;
	mb();

	/* send a rdma command to the PCIe engine, and wait for the
	 * response in the confirmation address.  The firmware should
	 * write a -1 there to indicate it is alive and well
	 */
	dma_low = MYRI10GE_LOWPART_TO_U32(mgp->cmd_bus);
	dma_high = MYRI10GE_HIGHPART_TO_U32(mgp->cmd_bus);

	buf[0] = htonl(dma_high);	/* confirm addr MSW */
	buf[1] = htonl(dma_low);	/* confirm addr LSW */
	buf[2] = MYRI10GE_NO_CONFIRM_DATA;	/* confirm data */
	buf[3] = htonl(dma_high);	/* dummy addr MSW */
	buf[4] = htonl(dma_low);	/* dummy addr LSW */
	buf[5] = htonl(enable);	/* enable? */

	submit = mgp->sram + MXGEFW_BOOT_DUMMY_RDMA;

	myri10ge_pio_copy(submit, &buf, sizeof(buf));
	for (i = 0; mgp->cmd->data != MYRI10GE_NO_CONFIRM_DATA && i < 20; i++)
		msleep(1);
	if (mgp->cmd->data != MYRI10GE_NO_CONFIRM_DATA)
		dev_err(&mgp->pdev->dev, "dummy rdma %s failed\n",
			(enable ? "enable" : "disable"));
}

static int
myri10ge_validate_firmware(struct myri10ge_priv *mgp,
			   struct mcp_gen_header *hdr)
{
	struct device *dev = &mgp->pdev->dev;
	int major, minor;

	/* check firmware type */
	if (ntohl(hdr->mcp_type) != MCP_TYPE_ETH) {
		dev_err(dev, "Bad firmware type: 0x%x\n", ntohl(hdr->mcp_type));
		return -EINVAL;
	}

	/* save firmware version for ethtool */
	strncpy(mgp->fw_version, hdr->version, sizeof(mgp->fw_version));

	sscanf(mgp->fw_version, "%d.%d", &major, &minor);

	if (!(major == MXGEFW_VERSION_MAJOR && minor == MXGEFW_VERSION_MINOR)) {
		dev_err(dev, "Found firmware version %s\n", mgp->fw_version);
		dev_err(dev, "Driver needs %d.%d\n", MXGEFW_VERSION_MAJOR,
			MXGEFW_VERSION_MINOR);
		return -EINVAL;
	}
	return 0;
}

static int myri10ge_load_hotplug_firmware(struct myri10ge_priv *mgp, u32 * size)
{
	unsigned crc, reread_crc;
	const struct firmware *fw;
	struct device *dev = &mgp->pdev->dev;
	struct mcp_gen_header *hdr;
	size_t hdr_offset;
	int status;
	unsigned i;

	if ((status = request_firmware(&fw, mgp->fw_name, dev)) < 0) {
		dev_err(dev, "Unable to load %s firmware image via hotplug\n",
			mgp->fw_name);
		status = -EINVAL;
		goto abort_with_nothing;
	}

	/* check size */

	if (fw->size >= mgp->sram_size - MYRI10GE_FW_OFFSET ||
	    fw->size < MCP_HEADER_PTR_OFFSET + 4) {
		dev_err(dev, "Firmware size invalid:%d\n", (int)fw->size);
		status = -EINVAL;
		goto abort_with_fw;
	}

	/* check id */
	hdr_offset = ntohl(*(__be32 *) (fw->data + MCP_HEADER_PTR_OFFSET));
	if ((hdr_offset & 3) || hdr_offset + sizeof(*hdr) > fw->size) {
		dev_err(dev, "Bad firmware file\n");
		status = -EINVAL;
		goto abort_with_fw;
	}
	hdr = (void *)(fw->data + hdr_offset);

	status = myri10ge_validate_firmware(mgp, hdr);
	if (status != 0)
		goto abort_with_fw;

	crc = crc32(~0, fw->data, fw->size);
	for (i = 0; i < fw->size; i += 256) {
		myri10ge_pio_copy(mgp->sram + MYRI10GE_FW_OFFSET + i,
				  fw->data + i,
				  min(256U, (unsigned)(fw->size - i)));
		mb();
		readb(mgp->sram);
	}
	/* corruption checking is good for parity recovery and buggy chipset */
	memcpy_fromio(fw->data, mgp->sram + MYRI10GE_FW_OFFSET, fw->size);
	reread_crc = crc32(~0, fw->data, fw->size);
	if (crc != reread_crc) {
		dev_err(dev, "CRC failed(fw-len=%u), got 0x%x (expect 0x%x)\n",
			(unsigned)fw->size, reread_crc, crc);
		status = -EIO;
		goto abort_with_fw;
	}
	*size = (u32) fw->size;

abort_with_fw:
	release_firmware(fw);

abort_with_nothing:
	return status;
}

static int myri10ge_adopt_running_firmware(struct myri10ge_priv *mgp)
{
	struct mcp_gen_header *hdr;
	struct device *dev = &mgp->pdev->dev;
	const size_t bytes = sizeof(struct mcp_gen_header);
	size_t hdr_offset;
	int status;

	/* find running firmware header */
	hdr_offset = ntohl(__raw_readl(mgp->sram + MCP_HEADER_PTR_OFFSET));

	if ((hdr_offset & 3) || hdr_offset + sizeof(*hdr) > mgp->sram_size) {
		dev_err(dev, "Running firmware has bad header offset (%d)\n",
			(int)hdr_offset);
		return -EIO;
	}

	/* copy header of running firmware from SRAM to host memory to
	 * validate firmware */
	hdr = kmalloc(bytes, GFP_KERNEL);
	if (hdr == NULL) {
		dev_err(dev, "could not malloc firmware hdr\n");
		return -ENOMEM;
	}
	memcpy_fromio(hdr, mgp->sram + hdr_offset, bytes);
	status = myri10ge_validate_firmware(mgp, hdr);
	kfree(hdr);
	return status;
}

static int myri10ge_load_firmware(struct myri10ge_priv *mgp)
{
	char __iomem *submit;
	__be32 buf[16];
	u32 dma_low, dma_high, size;
	int status, i;

	size = 0;
	status = myri10ge_load_hotplug_firmware(mgp, &size);
	if (status) {
		dev_warn(&mgp->pdev->dev, "hotplug firmware loading failed\n");

		/* Do not attempt to adopt firmware if there
		 * was a bad crc */
		if (status == -EIO)
			return status;

		status = myri10ge_adopt_running_firmware(mgp);
		if (status != 0) {
			dev_err(&mgp->pdev->dev,
				"failed to adopt running firmware\n");
			return status;
		}
		dev_info(&mgp->pdev->dev,
			 "Successfully adopted running firmware\n");
		if (mgp->tx.boundary == 4096) {
			dev_warn(&mgp->pdev->dev,
				 "Using firmware currently running on NIC"
				 ".  For optimal\n");
			dev_warn(&mgp->pdev->dev,
				 "performance consider loading optimized "
				 "firmware\n");
			dev_warn(&mgp->pdev->dev, "via hotplug\n");
		}

		mgp->fw_name = "adopted";
		mgp->tx.boundary = 2048;
		return status;
	}

	/* clear confirmation addr */
	mgp->cmd->data = 0;
	mb();

	/* send a reload command to the bootstrap MCP, and wait for the
	 *  response in the confirmation address.  The firmware should
	 * write a -1 there to indicate it is alive and well
	 */
	dma_low = MYRI10GE_LOWPART_TO_U32(mgp->cmd_bus);
	dma_high = MYRI10GE_HIGHPART_TO_U32(mgp->cmd_bus);

	buf[0] = htonl(dma_high);	/* confirm addr MSW */
	buf[1] = htonl(dma_low);	/* confirm addr LSW */
	buf[2] = MYRI10GE_NO_CONFIRM_DATA;	/* confirm data */

	/* FIX: All newest firmware should un-protect the bottom of
	 * the sram before handoff. However, the very first interfaces
	 * do not. Therefore the handoff copy must skip the first 8 bytes
	 */
	buf[3] = htonl(MYRI10GE_FW_OFFSET + 8);	/* where the code starts */
	buf[4] = htonl(size - 8);	/* length of code */
	buf[5] = htonl(8);	/* where to copy to */
	buf[6] = htonl(0);	/* where to jump to */

	submit = mgp->sram + MXGEFW_BOOT_HANDOFF;

	myri10ge_pio_copy(submit, &buf, sizeof(buf));
	mb();
	msleep(1);
	mb();
	i = 0;
	while (mgp->cmd->data != MYRI10GE_NO_CONFIRM_DATA && i < 20) {
		msleep(1);
		i++;
	}
	if (mgp->cmd->data != MYRI10GE_NO_CONFIRM_DATA) {
		dev_err(&mgp->pdev->dev, "handoff failed\n");
		return -ENXIO;
	}
	dev_info(&mgp->pdev->dev, "handoff confirmed\n");
	myri10ge_dummy_rdma(mgp, 1);

	return 0;
}

static int myri10ge_update_mac_address(struct myri10ge_priv *mgp, u8 * addr)
{
	struct myri10ge_cmd cmd;
	int status;

	cmd.data0 = ((addr[0] << 24) | (addr[1] << 16)
		     | (addr[2] << 8) | addr[3]);

	cmd.data1 = ((addr[4] << 8) | (addr[5]));

	status = myri10ge_send_cmd(mgp, MXGEFW_SET_MAC_ADDRESS, &cmd, 0);
	return status;
}

static int myri10ge_change_pause(struct myri10ge_priv *mgp, int pause)
{
	struct myri10ge_cmd cmd;
	int status, ctl;

	ctl = pause ? MXGEFW_ENABLE_FLOW_CONTROL : MXGEFW_DISABLE_FLOW_CONTROL;
	status = myri10ge_send_cmd(mgp, ctl, &cmd, 0);

	if (status) {
		printk(KERN_ERR
		       "myri10ge: %s: Failed to set flow control mode\n",
		       mgp->dev->name);
		return status;
	}
	mgp->pause = pause;
	return 0;
}

static void
myri10ge_change_promisc(struct myri10ge_priv *mgp, int promisc, int atomic)
{
	struct myri10ge_cmd cmd;
	int status, ctl;

	ctl = promisc ? MXGEFW_ENABLE_PROMISC : MXGEFW_DISABLE_PROMISC;
	status = myri10ge_send_cmd(mgp, ctl, &cmd, atomic);
	if (status)
		printk(KERN_ERR "myri10ge: %s: Failed to set promisc mode\n",
		       mgp->dev->name);
}

static int myri10ge_reset(struct myri10ge_priv *mgp)
{
	struct myri10ge_cmd cmd;
	int status;
	size_t bytes;
	u32 len;

	/* try to send a reset command to the card to see if it
	 * is alive */
	memset(&cmd, 0, sizeof(cmd));
	status = myri10ge_send_cmd(mgp, MXGEFW_CMD_RESET, &cmd, 0);
	if (status != 0) {
		dev_err(&mgp->pdev->dev, "failed reset\n");
		return -ENXIO;
	}

	/* Now exchange information about interrupts  */

	bytes = myri10ge_max_intr_slots * sizeof(*mgp->rx_done.entry);
	memset(mgp->rx_done.entry, 0, bytes);
	cmd.data0 = (u32) bytes;
	status = myri10ge_send_cmd(mgp, MXGEFW_CMD_SET_INTRQ_SIZE, &cmd, 0);
	cmd.data0 = MYRI10GE_LOWPART_TO_U32(mgp->rx_done.bus);
	cmd.data1 = MYRI10GE_HIGHPART_TO_U32(mgp->rx_done.bus);
	status |= myri10ge_send_cmd(mgp, MXGEFW_CMD_SET_INTRQ_DMA, &cmd, 0);

	status |=
	    myri10ge_send_cmd(mgp, MXGEFW_CMD_GET_IRQ_ACK_OFFSET, &cmd, 0);
	mgp->irq_claim = (__iomem __be32 *) (mgp->sram + cmd.data0);
	if (!mgp->msi_enabled) {
		status |= myri10ge_send_cmd
		    (mgp, MXGEFW_CMD_GET_IRQ_DEASSERT_OFFSET, &cmd, 0);
		mgp->irq_deassert = (__iomem __be32 *) (mgp->sram + cmd.data0);

	}
	status |= myri10ge_send_cmd
	    (mgp, MXGEFW_CMD_GET_INTR_COAL_DELAY_OFFSET, &cmd, 0);
	mgp->intr_coal_delay_ptr = (__iomem __be32 *) (mgp->sram + cmd.data0);
	if (status != 0) {
		dev_err(&mgp->pdev->dev, "failed set interrupt parameters\n");
		return status;
	}
	put_be32(htonl(mgp->intr_coal_delay), mgp->intr_coal_delay_ptr);

	/* Run a small DMA test.
	 * The magic multipliers to the length tell the firmware
	 * to do DMA read, write, or read+write tests.  The
	 * results are returned in cmd.data0.  The upper 16
	 * bits or the return is the number of transfers completed.
	 * The lower 16 bits is the time in 0.5us ticks that the
	 * transfers took to complete.
	 */

	len = mgp->tx.boundary;

	cmd.data0 = MYRI10GE_LOWPART_TO_U32(mgp->rx_done.bus);
	cmd.data1 = MYRI10GE_HIGHPART_TO_U32(mgp->rx_done.bus);
	cmd.data2 = len * 0x10000;
	status = myri10ge_send_cmd(mgp, MXGEFW_DMA_TEST, &cmd, 0);
	if (status == 0)
		mgp->read_dma = ((cmd.data0 >> 16) * len * 2) /
		    (cmd.data0 & 0xffff);
	else
		dev_warn(&mgp->pdev->dev, "DMA read benchmark failed: %d\n",
			 status);
	cmd.data0 = MYRI10GE_LOWPART_TO_U32(mgp->rx_done.bus);
	cmd.data1 = MYRI10GE_HIGHPART_TO_U32(mgp->rx_done.bus);
	cmd.data2 = len * 0x1;
	status = myri10ge_send_cmd(mgp, MXGEFW_DMA_TEST, &cmd, 0);
	if (status == 0)
		mgp->write_dma = ((cmd.data0 >> 16) * len * 2) /
		    (cmd.data0 & 0xffff);
	else
		dev_warn(&mgp->pdev->dev, "DMA write benchmark failed: %d\n",
			 status);

	cmd.data0 = MYRI10GE_LOWPART_TO_U32(mgp->rx_done.bus);
	cmd.data1 = MYRI10GE_HIGHPART_TO_U32(mgp->rx_done.bus);
	cmd.data2 = len * 0x10001;
	status = myri10ge_send_cmd(mgp, MXGEFW_DMA_TEST, &cmd, 0);
	if (status == 0)
		mgp->read_write_dma = ((cmd.data0 >> 16) * len * 2 * 2) /
		    (cmd.data0 & 0xffff);
	else
		dev_warn(&mgp->pdev->dev,
			 "DMA read/write benchmark failed: %d\n", status);

	memset(mgp->rx_done.entry, 0, bytes);

	/* reset mcp/driver shared state back to 0 */
	mgp->tx.req = 0;
	mgp->tx.done = 0;
	mgp->tx.pkt_start = 0;
	mgp->tx.pkt_done = 0;
	mgp->rx_big.cnt = 0;
	mgp->rx_small.cnt = 0;
	mgp->rx_done.idx = 0;
	mgp->rx_done.cnt = 0;
	mgp->link_changes = 0;
	status = myri10ge_update_mac_address(mgp, mgp->dev->dev_addr);
	myri10ge_change_promisc(mgp, 0, 0);
	myri10ge_change_pause(mgp, mgp->pause);
	return status;
}

static inline void
myri10ge_submit_8rx(struct mcp_kreq_ether_recv __iomem * dst,
		    struct mcp_kreq_ether_recv *src)
{
	__be32 low;

	low = src->addr_low;
	src->addr_low = htonl(DMA_32BIT_MASK);
	myri10ge_pio_copy(dst, src, 4 * sizeof(*src));
	mb();
	myri10ge_pio_copy(dst + 4, src + 4, 4 * sizeof(*src));
	mb();
	src->addr_low = low;
	put_be32(low, &dst->addr_low);
	mb();
}

/*
 * Set of routines to get a new receive buffer.  Any buffer which
 * crosses a 4KB boundary must start on a 4KB boundary due to PCIe
 * wdma restrictions. We also try to align any smaller allocation to
 * at least a 16 byte boundary for efficiency.  We assume the linux
 * memory allocator works by powers of 2, and will not return memory
 * smaller than 2KB which crosses a 4KB boundary.  If it does, we fall
 * back to allocating 2x as much space as required.
 *
 * We intend to replace large (>4KB) skb allocations by using
 * pages directly and building a fraglist in the near future.
 */

static inline struct sk_buff *myri10ge_alloc_big(struct net_device *dev,
						 int bytes)
{
	struct sk_buff *skb;
	unsigned long data, roundup;

	skb = netdev_alloc_skb(dev, bytes + 4096 + MXGEFW_PAD);
	if (skb == NULL)
		return NULL;

	/* Correct skb->truesize so that socket buffer
	 * accounting is not confused the rounding we must
	 * do to satisfy alignment constraints.
	 */
	skb->truesize -= 4096;

	data = (unsigned long)(skb->data);
	roundup = (-data) & (4095);
	skb_reserve(skb, roundup);
	return skb;
}

/* Allocate 2x as much space as required and use whichever portion
 * does not cross a 4KB boundary */
static inline struct sk_buff *myri10ge_alloc_small_safe(struct net_device *dev,
							unsigned int bytes)
{
	struct sk_buff *skb;
	unsigned long data, boundary;

	skb = netdev_alloc_skb(dev, 2 * (bytes + MXGEFW_PAD) - 1);
	if (unlikely(skb == NULL))
		return NULL;

	/* Correct skb->truesize so that socket buffer
	 * accounting is not confused the rounding we must
	 * do to satisfy alignment constraints.
	 */
	skb->truesize -= bytes + MXGEFW_PAD;

	data = (unsigned long)(skb->data);
	boundary = (data + 4095UL) & ~4095UL;
	if ((boundary - data) >= (bytes + MXGEFW_PAD))
		return skb;

	skb_reserve(skb, boundary - data);
	return skb;
}

/* Allocate just enough space, and verify that the allocated
 * space does not cross a 4KB boundary */
static inline struct sk_buff *myri10ge_alloc_small(struct net_device *dev,
						   int bytes)
{
	struct sk_buff *skb;
	unsigned long roundup, data, end;

	skb = netdev_alloc_skb(dev, bytes + 16 + MXGEFW_PAD);
	if (unlikely(skb == NULL))
		return NULL;

	/* Round allocated buffer to 16 byte boundary */
	data = (unsigned long)(skb->data);
	roundup = (-data) & 15UL;
	skb_reserve(skb, roundup);
	/* Verify that the data buffer does not cross a page boundary */
	data = (unsigned long)(skb->data);
	end = data + bytes + MXGEFW_PAD - 1;
	if (unlikely(((end >> 12) != (data >> 12)) && (data & 4095UL))) {
		printk(KERN_NOTICE
		       "myri10ge_alloc_small: small skb crossed 4KB boundary\n");
		myri10ge_skb_cross_4k = 1;
		dev_kfree_skb_any(skb);
		skb = myri10ge_alloc_small_safe(dev, bytes);
	}
	return skb;
}

static inline int
myri10ge_getbuf(struct myri10ge_rx_buf *rx, struct myri10ge_priv *mgp,
		int bytes, int idx)
{
	struct net_device *dev = mgp->dev;
	struct pci_dev *pdev = mgp->pdev;
	struct sk_buff *skb;
	dma_addr_t bus;
	int len, retval = 0;

	bytes += VLAN_HLEN;	/* account for 802.1q vlan tag */

	if ((bytes + MXGEFW_PAD) > (4096 - 16) /* linux overhead */ )
		skb = myri10ge_alloc_big(dev, bytes);
	else if (myri10ge_skb_cross_4k)
		skb = myri10ge_alloc_small_safe(dev, bytes);
	else
		skb = myri10ge_alloc_small(dev, bytes);

	if (unlikely(skb == NULL)) {
		rx->alloc_fail++;
		retval = -ENOBUFS;
		goto done;
	}

	/* set len so that it only covers the area we
	 * need mapped for DMA */
	len = bytes + MXGEFW_PAD;

	bus = pci_map_single(pdev, skb->data, len, PCI_DMA_FROMDEVICE);
	rx->info[idx].skb = skb;
	pci_unmap_addr_set(&rx->info[idx], bus, bus);
	pci_unmap_len_set(&rx->info[idx], len, len);
	rx->shadow[idx].addr_low = htonl(MYRI10GE_LOWPART_TO_U32(bus));
	rx->shadow[idx].addr_high = htonl(MYRI10GE_HIGHPART_TO_U32(bus));

done:
	/* copy 8 descriptors (64-bytes) to the mcp at a time */
	if ((idx & 7) == 7) {
		if (rx->wc_fifo == NULL)
			myri10ge_submit_8rx(&rx->lanai[idx - 7],
					    &rx->shadow[idx - 7]);
		else {
			mb();
			myri10ge_pio_copy(rx->wc_fifo,
					  &rx->shadow[idx - 7], 64);
		}
	}
	return retval;
}

static inline void myri10ge_vlan_ip_csum(struct sk_buff *skb, __wsum hw_csum)
{
	struct vlan_hdr *vh = (struct vlan_hdr *)(skb->data);

	if ((skb->protocol == htons(ETH_P_8021Q)) &&
	    (vh->h_vlan_encapsulated_proto == htons(ETH_P_IP) ||
	     vh->h_vlan_encapsulated_proto == htons(ETH_P_IPV6))) {
		skb->csum = hw_csum;
		skb->ip_summed = CHECKSUM_COMPLETE;
	}
}

static inline void
myri10ge_rx_skb_build(struct sk_buff *skb, u8 * va,
		      struct skb_frag_struct *rx_frags, int len, int hlen)
{
	struct skb_frag_struct *skb_frags;

	skb->len = skb->data_len = len;
	skb->truesize = len + sizeof(struct sk_buff);
	/* attach the page(s) */

	skb_frags = skb_shinfo(skb)->frags;
	while (len > 0) {
		memcpy(skb_frags, rx_frags, sizeof(*skb_frags));
		len -= rx_frags->size;
		skb_frags++;
		rx_frags++;
		skb_shinfo(skb)->nr_frags++;
	}

	/* pskb_may_pull is not available in irq context, but
	 * skb_pull() (for ether_pad and eth_type_trans()) requires
	 * the beginning of the packet in skb_headlen(), move it
	 * manually */
	memcpy(skb->data, va, hlen);
	skb_shinfo(skb)->frags[0].page_offset += hlen;
	skb_shinfo(skb)->frags[0].size -= hlen;
	skb->data_len -= hlen;
	skb->tail += hlen;
	skb_pull(skb, MXGEFW_PAD);
}

static void
myri10ge_alloc_rx_pages(struct myri10ge_priv *mgp, struct myri10ge_rx_buf *rx,
			int bytes, int watchdog)
{
	struct page *page;
	int idx;

	if (unlikely(rx->watchdog_needed && !watchdog))
		return;

	/* try to refill entire ring */
	while (rx->fill_cnt != (rx->cnt + rx->mask + 1)) {
		idx = rx->fill_cnt & rx->mask;

		if ((bytes < MYRI10GE_ALLOC_SIZE / 2) &&
		    (rx->page_offset + bytes <= MYRI10GE_ALLOC_SIZE)) {
			/* we can use part of previous page */
			get_page(rx->page);
		} else {
			/* we need a new page */
			page =
			    alloc_pages(GFP_ATOMIC | __GFP_COMP,
					MYRI10GE_ALLOC_ORDER);
			if (unlikely(page == NULL)) {
				if (rx->fill_cnt - rx->cnt < 16)
					rx->watchdog_needed = 1;
				return;
			}
			rx->page = page;
			rx->page_offset = 0;
			rx->bus = pci_map_page(mgp->pdev, page, 0,
					       MYRI10GE_ALLOC_SIZE,
					       PCI_DMA_FROMDEVICE);
		}
		rx->info[idx].page = rx->page;
		rx->info[idx].page_offset = rx->page_offset;
		/* note that this is the address of the start of the
		 * page */
		pci_unmap_addr_set(&rx->info[idx], bus, rx->bus);
		rx->shadow[idx].addr_low =
		    htonl(MYRI10GE_LOWPART_TO_U32(rx->bus) + rx->page_offset);
		rx->shadow[idx].addr_high =
		    htonl(MYRI10GE_HIGHPART_TO_U32(rx->bus));

		/* start next packet on a cacheline boundary */
		rx->page_offset += SKB_DATA_ALIGN(bytes);
		rx->fill_cnt++;

		/* copy 8 descriptors to the firmware at a time */
		if ((idx & 7) == 7) {
			if (rx->wc_fifo == NULL)
				myri10ge_submit_8rx(&rx->lanai[idx - 7],
						    &rx->shadow[idx - 7]);
			else {
				mb();
				myri10ge_pio_copy(rx->wc_fifo,
						  &rx->shadow[idx - 7], 64);
			}
		}
	}
}

static inline void
myri10ge_unmap_rx_page(struct pci_dev *pdev,
		       struct myri10ge_rx_buffer_state *info, int bytes)
{
	/* unmap the recvd page if we're the only or last user of it */
	if (bytes >= MYRI10GE_ALLOC_SIZE / 2 ||
	    (info->page_offset + 2 * bytes) > MYRI10GE_ALLOC_SIZE) {
		pci_unmap_page(pdev, (pci_unmap_addr(info, bus)
				      & ~(MYRI10GE_ALLOC_SIZE - 1)),
			       MYRI10GE_ALLOC_SIZE, PCI_DMA_FROMDEVICE);
	}
}

#define MYRI10GE_HLEN 64	/* The number of bytes to copy from a
				 * page into an skb */

static inline int
myri10ge_page_rx_done(struct myri10ge_priv *mgp, struct myri10ge_rx_buf *rx,
		      int bytes, int len, __wsum csum)
{
	struct sk_buff *skb;
	struct skb_frag_struct rx_frags[MYRI10GE_MAX_FRAGS_PER_FRAME];
	int i, idx, hlen, remainder;
	struct pci_dev *pdev = mgp->pdev;
	struct net_device *dev = mgp->dev;
	u8 *va;

	len += MXGEFW_PAD;
	idx = rx->cnt & rx->mask;
	va = page_address(rx->info[idx].page) + rx->info[idx].page_offset;
	prefetch(va);
	/* Fill skb_frag_struct(s) with data from our receive */
	for (i = 0, remainder = len; remainder > 0; i++) {
		myri10ge_unmap_rx_page(pdev, &rx->info[idx], bytes);
		rx_frags[i].page = rx->info[idx].page;
		rx_frags[i].page_offset = rx->info[idx].page_offset;
		if (remainder < MYRI10GE_ALLOC_SIZE)
			rx_frags[i].size = remainder;
		else
			rx_frags[i].size = MYRI10GE_ALLOC_SIZE;
		rx->cnt++;
		idx = rx->cnt & rx->mask;
		remainder -= MYRI10GE_ALLOC_SIZE;
	}

	hlen = MYRI10GE_HLEN > len ? len : MYRI10GE_HLEN;

	/* allocate an skb to attach the page(s) to. */

	skb = netdev_alloc_skb(dev, MYRI10GE_HLEN + 16);
	if (unlikely(skb == NULL)) {
		mgp->stats.rx_dropped++;
		do {
			i--;
			put_page(rx_frags[i].page);
		} while (i != 0);
		return 0;
	}

	/* Attach the pages to the skb, and trim off any padding */
	myri10ge_rx_skb_build(skb, va, rx_frags, len, hlen);
	if (skb_shinfo(skb)->frags[0].size <= 0) {
		put_page(skb_shinfo(skb)->frags[0].page);
		skb_shinfo(skb)->nr_frags = 0;
	}
	skb->protocol = eth_type_trans(skb, dev);
	skb->dev = dev;

	if (mgp->csum_flag) {
		if ((skb->protocol == htons(ETH_P_IP)) ||
		    (skb->protocol == htons(ETH_P_IPV6))) {
			skb->csum = csum;
			skb->ip_summed = CHECKSUM_COMPLETE;
		} else
			myri10ge_vlan_ip_csum(skb, csum);
	}
	netif_receive_skb(skb);
	dev->last_rx = jiffies;
	return 1;
}

static inline unsigned long
myri10ge_rx_done(struct myri10ge_priv *mgp, struct myri10ge_rx_buf *rx,
		 int bytes, int len, __wsum csum)
{
	dma_addr_t bus;
	struct sk_buff *skb;
	int idx, unmap_len;

	idx = rx->cnt & rx->mask;
	rx->cnt++;

	/* save a pointer to the received skb */
	skb = rx->info[idx].skb;
	bus = pci_unmap_addr(&rx->info[idx], bus);
	unmap_len = pci_unmap_len(&rx->info[idx], len);

	/* try to replace the received skb */
	if (myri10ge_getbuf(rx, mgp, bytes, idx)) {
		/* drop the frame -- the old skbuf is re-cycled */
		mgp->stats.rx_dropped += 1;
		return 0;
	}

	/* unmap the recvd skb */
	pci_unmap_single(mgp->pdev, bus, unmap_len, PCI_DMA_FROMDEVICE);

	/* mcp implicitly skips 1st bytes so that packet is properly
	 * aligned */
	skb_reserve(skb, MXGEFW_PAD);

	/* set the length of the frame */
	skb_put(skb, len);

	skb->protocol = eth_type_trans(skb, mgp->dev);
	if (mgp->csum_flag) {
		if ((skb->protocol == htons(ETH_P_IP)) ||
		    (skb->protocol == htons(ETH_P_IPV6))) {
			skb->csum = csum;
			skb->ip_summed = CHECKSUM_COMPLETE;
		} else
			myri10ge_vlan_ip_csum(skb, csum);
	}

	netif_receive_skb(skb);
	mgp->dev->last_rx = jiffies;
	return 1;
}

static inline void myri10ge_tx_done(struct myri10ge_priv *mgp, int mcp_index)
{
	struct pci_dev *pdev = mgp->pdev;
	struct myri10ge_tx_buf *tx = &mgp->tx;
	struct sk_buff *skb;
	int idx, len;
	int limit = 0;

	while (tx->pkt_done != mcp_index) {
		idx = tx->done & tx->mask;
		skb = tx->info[idx].skb;

		/* Mark as free */
		tx->info[idx].skb = NULL;
		if (tx->info[idx].last) {
			tx->pkt_done++;
			tx->info[idx].last = 0;
		}
		tx->done++;
		len = pci_unmap_len(&tx->info[idx], len);
		pci_unmap_len_set(&tx->info[idx], len, 0);
		if (skb) {
			mgp->stats.tx_bytes += skb->len;
			mgp->stats.tx_packets++;
			dev_kfree_skb_irq(skb);
			if (len)
				pci_unmap_single(pdev,
						 pci_unmap_addr(&tx->info[idx],
								bus), len,
						 PCI_DMA_TODEVICE);
		} else {
			if (len)
				pci_unmap_page(pdev,
					       pci_unmap_addr(&tx->info[idx],
							      bus), len,
					       PCI_DMA_TODEVICE);
		}

		/* limit potential for livelock by only handling
		 * 2 full tx rings per call */
		if (unlikely(++limit > 2 * tx->mask))
			break;
	}
	/* start the queue if we've stopped it */
	if (netif_queue_stopped(mgp->dev)
	    && tx->req - tx->done < (tx->mask >> 1)) {
		mgp->wake_queue++;
		netif_wake_queue(mgp->dev);
	}
}

static inline void myri10ge_clean_rx_done(struct myri10ge_priv *mgp, int *limit)
{
	struct myri10ge_rx_done *rx_done = &mgp->rx_done;
	unsigned long rx_bytes = 0;
	unsigned long rx_packets = 0;
	unsigned long rx_ok;

	int idx = rx_done->idx;
	int cnt = rx_done->cnt;
	u16 length;
	__wsum checksum;

	while (rx_done->entry[idx].length != 0 && *limit != 0) {
		length = ntohs(rx_done->entry[idx].length);
		rx_done->entry[idx].length = 0;
		checksum = csum_unfold(rx_done->entry[idx].checksum);
		if (length <= mgp->small_bytes)
			rx_ok = myri10ge_rx_done(mgp, &mgp->rx_small,
						 mgp->small_bytes,
						 length, checksum);
		else
			rx_ok = myri10ge_rx_done(mgp, &mgp->rx_big,
						 mgp->dev->mtu + ETH_HLEN,
						 length, checksum);
		rx_packets += rx_ok;
		rx_bytes += rx_ok * (unsigned long)length;
		cnt++;
		idx = cnt & (myri10ge_max_intr_slots - 1);

		/* limit potential for livelock by only handling a
		 * limited number of frames. */
		(*limit)--;
	}
	rx_done->idx = idx;
	rx_done->cnt = cnt;
	mgp->stats.rx_packets += rx_packets;
	mgp->stats.rx_bytes += rx_bytes;
}

static inline void myri10ge_check_statblock(struct myri10ge_priv *mgp)
{
	struct mcp_irq_data *stats = mgp->fw_stats;

	if (unlikely(stats->stats_updated)) {
		if (mgp->link_state != stats->link_up) {
			mgp->link_state = stats->link_up;
			if (mgp->link_state) {
				if (netif_msg_link(mgp))
					printk(KERN_INFO
					       "myri10ge: %s: link up\n",
					       mgp->dev->name);
				netif_carrier_on(mgp->dev);
				mgp->link_changes++;
			} else {
				if (netif_msg_link(mgp))
					printk(KERN_INFO
					       "myri10ge: %s: link down\n",
					       mgp->dev->name);
				netif_carrier_off(mgp->dev);
				mgp->link_changes++;
			}
		}
		if (mgp->rdma_tags_available !=
		    ntohl(mgp->fw_stats->rdma_tags_available)) {
			mgp->rdma_tags_available =
			    ntohl(mgp->fw_stats->rdma_tags_available);
			printk(KERN_WARNING "myri10ge: %s: RDMA timed out! "
			       "%d tags left\n", mgp->dev->name,
			       mgp->rdma_tags_available);
		}
		mgp->down_cnt += stats->link_down;
		if (stats->link_down)
			wake_up(&mgp->down_wq);
	}
}

static int myri10ge_poll(struct net_device *netdev, int *budget)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);
	struct myri10ge_rx_done *rx_done = &mgp->rx_done;
	int limit, orig_limit, work_done;

	/* process as many rx events as NAPI will allow */
	limit = min(*budget, netdev->quota);
	orig_limit = limit;
	myri10ge_clean_rx_done(mgp, &limit);
	work_done = orig_limit - limit;
	*budget -= work_done;
	netdev->quota -= work_done;

	if (rx_done->entry[rx_done->idx].length == 0 || !netif_running(netdev)) {
		netif_rx_complete(netdev);
		put_be32(htonl(3), mgp->irq_claim);
		return 0;
	}
	return 1;
}

static irqreturn_t myri10ge_intr(int irq, void *arg)
{
	struct myri10ge_priv *mgp = arg;
	struct mcp_irq_data *stats = mgp->fw_stats;
	struct myri10ge_tx_buf *tx = &mgp->tx;
	u32 send_done_count;
	int i;

	/* make sure it is our IRQ, and that the DMA has finished */
	if (unlikely(!stats->valid))
		return (IRQ_NONE);

	/* low bit indicates receives are present, so schedule
	 * napi poll handler */
	if (stats->valid & 1)
		netif_rx_schedule(mgp->dev);

	if (!mgp->msi_enabled) {
		put_be32(0, mgp->irq_deassert);
		if (!myri10ge_deassert_wait)
			stats->valid = 0;
		mb();
	} else
		stats->valid = 0;

	/* Wait for IRQ line to go low, if using INTx */
	i = 0;
	while (1) {
		i++;
		/* check for transmit completes and receives */
		send_done_count = ntohl(stats->send_done_count);
		if (send_done_count != tx->pkt_done)
			myri10ge_tx_done(mgp, (int)send_done_count);
		if (unlikely(i > myri10ge_max_irq_loops)) {
			printk(KERN_WARNING "myri10ge: %s: irq stuck?\n",
			       mgp->dev->name);
			stats->valid = 0;
			schedule_work(&mgp->watchdog_work);
		}
		if (likely(stats->valid == 0))
			break;
		cpu_relax();
		barrier();
	}

	myri10ge_check_statblock(mgp);

	put_be32(htonl(3), mgp->irq_claim + 1);
	return (IRQ_HANDLED);
}

static int
myri10ge_get_settings(struct net_device *netdev, struct ethtool_cmd *cmd)
{
	cmd->autoneg = AUTONEG_DISABLE;
	cmd->speed = SPEED_10000;
	cmd->duplex = DUPLEX_FULL;
	return 0;
}

static void
myri10ge_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *info)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);

	strlcpy(info->driver, "myri10ge", sizeof(info->driver));
	strlcpy(info->version, MYRI10GE_VERSION_STR, sizeof(info->version));
	strlcpy(info->fw_version, mgp->fw_version, sizeof(info->fw_version));
	strlcpy(info->bus_info, pci_name(mgp->pdev), sizeof(info->bus_info));
}

static int
myri10ge_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *coal)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);
	coal->rx_coalesce_usecs = mgp->intr_coal_delay;
	return 0;
}

static int
myri10ge_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *coal)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);

	mgp->intr_coal_delay = coal->rx_coalesce_usecs;
	put_be32(htonl(mgp->intr_coal_delay), mgp->intr_coal_delay_ptr);
	return 0;
}

static void
myri10ge_get_pauseparam(struct net_device *netdev,
			struct ethtool_pauseparam *pause)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);

	pause->autoneg = 0;
	pause->rx_pause = mgp->pause;
	pause->tx_pause = mgp->pause;
}

static int
myri10ge_set_pauseparam(struct net_device *netdev,
			struct ethtool_pauseparam *pause)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);

	if (pause->tx_pause != mgp->pause)
		return myri10ge_change_pause(mgp, pause->tx_pause);
	if (pause->rx_pause != mgp->pause)
		return myri10ge_change_pause(mgp, pause->tx_pause);
	if (pause->autoneg != 0)
		return -EINVAL;
	return 0;
}

static void
myri10ge_get_ringparam(struct net_device *netdev,
		       struct ethtool_ringparam *ring)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);

	ring->rx_mini_max_pending = mgp->rx_small.mask + 1;
	ring->rx_max_pending = mgp->rx_big.mask + 1;
	ring->rx_jumbo_max_pending = 0;
	ring->tx_max_pending = mgp->rx_small.mask + 1;
	ring->rx_mini_pending = ring->rx_mini_max_pending;
	ring->rx_pending = ring->rx_max_pending;
	ring->rx_jumbo_pending = ring->rx_jumbo_max_pending;
	ring->tx_pending = ring->tx_max_pending;
}

static u32 myri10ge_get_rx_csum(struct net_device *netdev)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);
	if (mgp->csum_flag)
		return 1;
	else
		return 0;
}

static int myri10ge_set_rx_csum(struct net_device *netdev, u32 csum_enabled)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);
	if (csum_enabled)
		mgp->csum_flag = MXGEFW_FLAGS_CKSUM;
	else
		mgp->csum_flag = 0;
	return 0;
}

static const char myri10ge_gstrings_stats[][ETH_GSTRING_LEN] = {
	"rx_packets", "tx_packets", "rx_bytes", "tx_bytes", "rx_errors",
	"tx_errors", "rx_dropped", "tx_dropped", "multicast", "collisions",
	"rx_length_errors", "rx_over_errors", "rx_crc_errors",
	"rx_frame_errors", "rx_fifo_errors", "rx_missed_errors",
	"tx_aborted_errors", "tx_carrier_errors", "tx_fifo_errors",
	"tx_heartbeat_errors", "tx_window_errors",
	/* device-specific stats */
	"tx_boundary", "WC", "irq", "MSI",
	"read_dma_bw_MBs", "write_dma_bw_MBs", "read_write_dma_bw_MBs",
	"serial_number", "tx_pkt_start", "tx_pkt_done",
	"tx_req", "tx_done", "rx_small_cnt", "rx_big_cnt",
	"wake_queue", "stop_queue", "watchdog_resets", "tx_linearized",
	"link_changes", "link_up", "dropped_link_overflow",
	"dropped_link_error_or_filtered", "dropped_multicast_filtered",
	"dropped_runt", "dropped_overrun", "dropped_no_small_buffer",
	"dropped_no_big_buffer"
};

#define MYRI10GE_NET_STATS_LEN      21
#define MYRI10GE_STATS_LEN  sizeof(myri10ge_gstrings_stats) / ETH_GSTRING_LEN

static void
myri10ge_get_strings(struct net_device *netdev, u32 stringset, u8 * data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, *myri10ge_gstrings_stats,
		       sizeof(myri10ge_gstrings_stats));
		break;
	}
}

static int myri10ge_get_stats_count(struct net_device *netdev)
{
	return MYRI10GE_STATS_LEN;
}

static void
myri10ge_get_ethtool_stats(struct net_device *netdev,
			   struct ethtool_stats *stats, u64 * data)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);
	int i;

	for (i = 0; i < MYRI10GE_NET_STATS_LEN; i++)
		data[i] = ((unsigned long *)&mgp->stats)[i];

	data[i++] = (unsigned int)mgp->tx.boundary;
	data[i++] = (unsigned int)(mgp->mtrr >= 0);
	data[i++] = (unsigned int)mgp->pdev->irq;
	data[i++] = (unsigned int)mgp->msi_enabled;
	data[i++] = (unsigned int)mgp->read_dma;
	data[i++] = (unsigned int)mgp->write_dma;
	data[i++] = (unsigned int)mgp->read_write_dma;
	data[i++] = (unsigned int)mgp->serial_number;
	data[i++] = (unsigned int)mgp->tx.pkt_start;
	data[i++] = (unsigned int)mgp->tx.pkt_done;
	data[i++] = (unsigned int)mgp->tx.req;
	data[i++] = (unsigned int)mgp->tx.done;
	data[i++] = (unsigned int)mgp->rx_small.cnt;
	data[i++] = (unsigned int)mgp->rx_big.cnt;
	data[i++] = (unsigned int)mgp->wake_queue;
	data[i++] = (unsigned int)mgp->stop_queue;
	data[i++] = (unsigned int)mgp->watchdog_resets;
	data[i++] = (unsigned int)mgp->tx_linearized;
	data[i++] = (unsigned int)mgp->link_changes;
	data[i++] = (unsigned int)ntohl(mgp->fw_stats->link_up);
	data[i++] = (unsigned int)ntohl(mgp->fw_stats->dropped_link_overflow);
	data[i++] =
	    (unsigned int)ntohl(mgp->fw_stats->dropped_link_error_or_filtered);
	data[i++] =
	    (unsigned int)ntohl(mgp->fw_stats->dropped_multicast_filtered);
	data[i++] = (unsigned int)ntohl(mgp->fw_stats->dropped_runt);
	data[i++] = (unsigned int)ntohl(mgp->fw_stats->dropped_overrun);
	data[i++] = (unsigned int)ntohl(mgp->fw_stats->dropped_no_small_buffer);
	data[i++] = (unsigned int)ntohl(mgp->fw_stats->dropped_no_big_buffer);
}

static void myri10ge_set_msglevel(struct net_device *netdev, u32 value)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);
	mgp->msg_enable = value;
}

static u32 myri10ge_get_msglevel(struct net_device *netdev)
{
	struct myri10ge_priv *mgp = netdev_priv(netdev);
	return mgp->msg_enable;
}

static const struct ethtool_ops myri10ge_ethtool_ops = {
	.get_settings = myri10ge_get_settings,
	.get_drvinfo = myri10ge_get_drvinfo,
	.get_coalesce = myri10ge_get_coalesce,
	.set_coalesce = myri10ge_set_coalesce,
	.get_pauseparam = myri10ge_get_pauseparam,
	.set_pauseparam = myri10ge_set_pauseparam,
	.get_ringparam = myri10ge_get_ringparam,
	.get_rx_csum = myri10ge_get_rx_csum,
	.set_rx_csum = myri10ge_set_rx_csum,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_hw_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
#ifdef NETIF_F_TSO
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
#endif
	.get_strings = myri10ge_get_strings,
	.get_stats_count = myri10ge_get_stats_count,
	.get_ethtool_stats = myri10ge_get_ethtool_stats,
	.set_msglevel = myri10ge_set_msglevel,
	.get_msglevel = myri10ge_get_msglevel
};

static int myri10ge_allocate_rings(struct net_device *dev)
{
	struct myri10ge_priv *mgp;
	struct myri10ge_cmd cmd;
	int tx_ring_size, rx_ring_size;
	int tx_ring_entries, rx_ring_entries;
	int i, status;
	size_t bytes;

	mgp = netdev_priv(dev);

	/* get ring sizes */

	status = myri10ge_send_cmd(mgp, MXGEFW_CMD_GET_SEND_RING_SIZE, &cmd, 0);
	tx_ring_size = cmd.data0;
	status |= myri10ge_send_cmd(mgp, MXGEFW_CMD_GET_RX_RING_SIZE, &cmd, 0);
	rx_ring_size = cmd.data0;

	tx_ring_entries = tx_ring_size / sizeof(struct mcp_kreq_ether_send);
	rx_ring_entries = rx_ring_size / sizeof(struct mcp_dma_addr);
	mgp->tx.mask = tx_ring_entries - 1;
	mgp->rx_small.mask = mgp->rx_big.mask = rx_ring_entries - 1;

	/* allocate the host shadow rings */

	bytes = 8 + (MYRI10GE_MAX_SEND_DESC_TSO + 4)
	    * sizeof(*mgp->tx.req_list);
	mgp->tx.req_bytes = kzalloc(bytes, GFP_KERNEL);
	if (mgp->tx.req_bytes == NULL)
		goto abort_with_nothing;

	/* ensure req_list entries are aligned to 8 bytes */
	mgp->tx.req_list = (struct mcp_kreq_ether_send *)
	    ALIGN((unsigned long)mgp->tx.req_bytes, 8);

	bytes = rx_ring_entries * sizeof(*mgp->rx_small.shadow);
	mgp->rx_small.shadow = kzalloc(bytes, GFP_KERNEL);
	if (mgp->rx_small.shadow == NULL)
		goto abort_with_tx_req_bytes;

	bytes = rx_ring_entries * sizeof(*mgp->rx_big.shadow);
	mgp->rx_big.shadow = kzalloc(bytes, GFP_KERNEL);
	if (mgp->rx_big.shadow == NULL)
		goto abort_with_rx_small_shadow;

	/* allocate the host info rings */

	bytes = tx_ring_entries * sizeof(*mgp->tx.info);
	mgp->tx.info = kzalloc(bytes, GFP_KERNEL);
	if (mgp->tx.info == NULL)
		goto abort_with_rx_big_shadow;

	bytes = rx_ring_entries * sizeof(*mgp->rx_small.info);
	mgp->rx_small.info = kzalloc(bytes, GFP_KERNEL);
	if (mgp->rx_small.info == NULL)
		goto abort_with_tx_info;

	bytes = rx_ring_entries * sizeof(*mgp->rx_big.info);
	mgp->rx_big.info = kzalloc(bytes, GFP_KERNEL);
	if (mgp->rx_big.info == NULL)
		goto abort_with_rx_small_info;

	/* Fill the receive rings */

	for (i = 0; i <= mgp->rx_small.mask; i++) {
		status = myri10ge_getbuf(&mgp->rx_small, mgp,
					 mgp->small_bytes, i);
		if (status) {
			printk(KERN_ERR
			       "myri10ge: %s: alloced only %d small bufs\n",
			       dev->name, i);
			goto abort_with_rx_small_ring;
		}
	}

	for (i = 0; i <= mgp->rx_big.mask; i++) {
		status =
		    myri10ge_getbuf(&mgp->rx_big, mgp, dev->mtu + ETH_HLEN, i);
		if (status) {
			printk(KERN_ERR
			       "myri10ge: %s: alloced only %d big bufs\n",
			       dev->name, i);
			goto abort_with_rx_big_ring;
		}
	}

	return 0;

abort_with_rx_big_ring:
	for (i = 0; i <= mgp->rx_big.mask; i++) {
		if (mgp->rx_big.info[i].skb != NULL)
			dev_kfree_skb_any(mgp->rx_big.info[i].skb);
		if (pci_unmap_len(&mgp->rx_big.info[i], len))
			pci_unmap_single(mgp->pdev,
					 pci_unmap_addr(&mgp->rx_big.info[i],
							bus),
					 pci_unmap_len(&mgp->rx_big.info[i],
						       len),
					 PCI_DMA_FROMDEVICE);
	}

abort_with_rx_small_ring:
	for (i = 0; i <= mgp->rx_small.mask; i++) {
		if (mgp->rx_small.info[i].skb != NULL)
			dev_kfree_skb_any(mgp->rx_small.info[i].skb);
		if (pci_unmap_len(&mgp->rx_small.info[i], len))
			pci_unmap_single(mgp->pdev,
					 pci_unmap_addr(&mgp->rx_small.info[i],
							bus),
					 pci_unmap_len(&mgp->rx_small.info[i],
						       len),
					 PCI_DMA_FROMDEVICE);
	}
	kfree(mgp->rx_big.info);

abort_with_rx_small_info:
	kfree(mgp->rx_small.info);

abort_with_tx_info:
	kfree(mgp->tx.info);

abort_with_rx_big_shadow:
	kfree(mgp->rx_big.shadow);

abort_with_rx_small_shadow:
	kfree(mgp->rx_small.shadow);

abort_with_tx_req_bytes:
	kfree(mgp->tx.req_bytes);
	mgp->tx.req_bytes = NULL;
	mgp->tx.req_list = NULL;

abort_with_nothing:
	return status;
}

static void myri10ge_free_rings(struct net_device *dev)
{
	struct myri10ge_priv *mgp;
	struct sk_buff *skb;
	struct myri10ge_tx_buf *tx;
	int i, len, idx;

	mgp = netdev_priv(dev);

	for (i = 0; i <= mgp->rx_big.mask; i++) {
		if (mgp->rx_big.info[i].skb != NULL)
			dev_kfree_skb_any(mgp->rx_big.info[i].skb);
		if (pci_unmap_len(&mgp->rx_big.info[i], len))
			pci_unmap_single(mgp->pdev,
					 pci_unmap_addr(&mgp->rx_big.info[i],
							bus),
					 pci_unmap_len(&mgp->rx_big.info[i],
						       len),
					 PCI_DMA_FROMDEVICE);
	}

	for (i = 0; i <= mgp->rx_small.mask; i++) {
		if (mgp->rx_small.info[i].skb != NULL)
			dev_kfree_skb_any(mgp->rx_small.info[i].skb);
		if (pci_unmap_len(&mgp->rx_small.info[i], len))
			pci_unmap_single(mgp->pdev,
					 pci_unmap_addr(&mgp->rx_small.info[i],
							bus),
					 pci_unmap_len(&mgp->rx_small.info[i],
						       len),
					 PCI_DMA_FROMDEVICE);
	}

	tx = &mgp->tx;
	while (tx->done != tx->req) {
		idx = tx->done & tx->mask;
		skb = tx->info[idx].skb;

		/* Mark as free */
		tx->info[idx].skb = NULL;
		tx->done++;
		len = pci_unmap_len(&tx->info[idx], len);
		pci_unmap_len_set(&tx->info[idx], len, 0);
		if (skb) {
			mgp->stats.tx_dropped++;
			dev_kfree_skb_any(skb);
			if (len)
				pci_unmap_single(mgp->pdev,
						 pci_unmap_addr(&tx->info[idx],
								bus), len,
						 PCI_DMA_TODEVICE);
		} else {
			if (len)
				pci_unmap_page(mgp->pdev,
					       pci_unmap_addr(&tx->info[idx],
							      bus), len,
					       PCI_DMA_TODEVICE);
		}
	}
	kfree(mgp->rx_big.info);

	kfree(mgp->rx_small.info);

	kfree(mgp->tx.info);

	kfree(mgp->rx_big.shadow);

	kfree(mgp->rx_small.shadow);

	kfree(mgp->tx.req_bytes);
	mgp->tx.req_bytes = NULL;
	mgp->tx.req_list = NULL;
}

static int myri10ge_open(struct net_device *dev)
{
	struct myri10ge_priv *mgp;
	struct myri10ge_cmd cmd;
	int status, big_pow2;

	mgp = netdev_priv(dev);

	if (mgp->running != MYRI10GE_ETH_STOPPED)
		return -EBUSY;

	mgp->running = MYRI10GE_ETH_STARTING;
	status = myri10ge_reset(mgp);
	if (status != 0) {
		printk(KERN_ERR "myri10ge: %s: failed reset\n", dev->name);
		mgp->running = MYRI10GE_ETH_STOPPED;
		return -ENXIO;
	}

	/* decide what small buffer size to use.  For good TCP rx
	 * performance, it is important to not receive 1514 byte
	 * frames into jumbo buffers, as it confuses the socket buffer
	 * accounting code, leading to drops and erratic performance.
	 */

	if (dev->mtu <= ETH_DATA_LEN)
		mgp->small_bytes = 128;	/* enough for a TCP header */
	else
		mgp->small_bytes = ETH_FRAME_LEN;	/* enough for an ETH_DATA_LEN frame */

	/* Override the small buffer size? */
	if (myri10ge_small_bytes > 0)
		mgp->small_bytes = myri10ge_small_bytes;

	/* If the user sets an obscenely small MTU, adjust the small
	 * bytes down to nearly nothing */
	if (mgp->small_bytes >= (dev->mtu + ETH_HLEN))
		mgp->small_bytes = 64;

	/* get the lanai pointers to the send and receive rings */

	status |= myri10ge_send_cmd(mgp, MXGEFW_CMD_GET_SEND_OFFSET, &cmd, 0);
	mgp->tx.lanai =
	    (struct mcp_kreq_ether_send __iomem *)(mgp->sram + cmd.data0);

	status |=
	    myri10ge_send_cmd(mgp, MXGEFW_CMD_GET_SMALL_RX_OFFSET, &cmd, 0);
	mgp->rx_small.lanai =
	    (struct mcp_kreq_ether_recv __iomem *)(mgp->sram + cmd.data0);

	status |= myri10ge_send_cmd(mgp, MXGEFW_CMD_GET_BIG_RX_OFFSET, &cmd, 0);
	mgp->rx_big.lanai =
	    (struct mcp_kreq_ether_recv __iomem *)(mgp->sram + cmd.data0);

	if (status != 0) {
		printk(KERN_ERR
		       "myri10ge: %s: failed to get ring sizes or locations\n",
		       dev->name);
		mgp->running = MYRI10GE_ETH_STOPPED;
		return -ENXIO;
	}

	if (mgp->mtrr >= 0) {
		mgp->tx.wc_fifo = (u8 __iomem *) mgp->sram + MXGEFW_ETH_SEND_4;
		mgp->rx_small.wc_fifo =
		    (u8 __iomem *) mgp->sram + MXGEFW_ETH_RECV_SMALL;
		mgp->rx_big.wc_fifo =
		    (u8 __iomem *) mgp->sram + MXGEFW_ETH_RECV_BIG;
	} else {
		mgp->tx.wc_fifo = NULL;
		mgp->rx_small.wc_fifo = NULL;
		mgp->rx_big.wc_fifo = NULL;
	}

	status = myri10ge_allocate_rings(dev);
	if (status != 0)
		goto abort_with_nothing;

	/* Firmware needs the big buff size as a power of 2.  Lie and
	 * tell him the buffer is larger, because we only use 1
	 * buffer/pkt, and the mtu will prevent overruns.
	 */
	big_pow2 = dev->mtu + ETH_HLEN + MXGEFW_PAD;
	while ((big_pow2 & (big_pow2 - 1)) != 0)
		big_pow2++;

	/* now give firmware buffers sizes, and MTU */
	cmd.data0 = dev->mtu + ETH_HLEN + VLAN_HLEN;
	status = myri10ge_send_cmd(mgp, MXGEFW_CMD_SET_MTU, &cmd, 0);
	cmd.data0 = mgp->small_bytes;
	status |=
	    myri10ge_send_cmd(mgp, MXGEFW_CMD_SET_SMALL_BUFFER_SIZE, &cmd, 0);
	cmd.data0 = big_pow2;
	status |=
	    myri10ge_send_cmd(mgp, MXGEFW_CMD_SET_BIG_BUFFER_SIZE, &cmd, 0);
	if (status) {
		printk(KERN_ERR "myri10ge: %s: Couldn't set buffer sizes\n",
		       dev->name);
		goto abort_with_rings;
	}

	cmd.data0 = MYRI10GE_LOWPART_TO_U32(mgp->fw_stats_bus);
	cmd.data1 = MYRI10GE_HIGHPART_TO_U32(mgp->fw_stats_bus);
	cmd.data2 = sizeof(struct mcp_irq_data);
	status = myri10ge_send_cmd(mgp, MXGEFW_CMD_SET_STATS_DMA_V2, &cmd, 0);
	if (status == -ENOSYS) {
		dma_addr_t bus = mgp->fw_stats_bus;
		bus += offsetof(struct mcp_irq_data, send_done_count);
		cmd.data0 = MYRI10GE_LOWPART_TO_U32(bus);
		cmd.data1 = MYRI10GE_HIGHPART_TO_U32(bus);
		status = myri10ge_send_cmd(mgp,
					   MXGEFW_CMD_SET_STATS_DMA_OBSOLETE,
					   &cmd, 0);
		/* Firmware cannot support multicast without STATS_DMA_V2 */
		mgp->fw_multicast_support = 0;
	} else {
		mgp->fw_multicast_support = 1;
	}
	if (status) {
		printk(KERN_ERR "myri10ge: %s: Couldn't set stats DMA\n",
		       dev->name);
		goto abort_with_rings;
	}

	mgp->link_state = htonl(~0U);
	mgp->rdma_tags_available = 15;

	netif_poll_enable(mgp->dev);	/* must happen prior to any irq */

	status = myri10ge_send_cmd(mgp, MXGEFW_CMD_ETHERNET_UP, &cmd, 0);
	if (status) {
		printk(KERN_ERR "myri10ge: %s: Couldn't bring up link\n",
		       dev->name);
		goto abort_with_rings;
	}

	mgp->wake_queue = 0;
	mgp->stop_queue = 0;
	mgp->running = MYRI10GE_ETH_RUNNING;
	mgp->watchdog_timer.expires = jiffies + myri10ge_watchdog_timeout * HZ;
	add_timer(&mgp->watchdog_timer);
	netif_wake_queue(dev);
	return 0;

abort_with_rings:
	myri10ge_free_rings(dev);

abort_with_nothing:
	mgp->running = MYRI10GE_ETH_STOPPED;
	return -ENOMEM;
}

static int myri10ge_close(struct net_device *dev)
{
	struct myri10ge_priv *mgp;
	struct myri10ge_cmd cmd;
	int status, old_down_cnt;

	mgp = netdev_priv(dev);

	if (mgp->running != MYRI10GE_ETH_RUNNING)
		return 0;

	if (mgp->tx.req_bytes == NULL)
		return 0;

	del_timer_sync(&mgp->watchdog_timer);
	mgp->running = MYRI10GE_ETH_STOPPING;
	netif_poll_disable(mgp->dev);
	netif_carrier_off(dev);
	netif_stop_queue(dev);
	old_down_cnt = mgp->down_cnt;
	mb();
	status = myri10ge_send_cmd(mgp, MXGEFW_CMD_ETHERNET_DOWN, &cmd, 0);
	if (status)
		printk(KERN_ERR "myri10ge: %s: Couldn't bring down link\n",
		       dev->name);

	wait_event_timeout(mgp->down_wq, old_down_cnt != mgp->down_cnt, HZ);
	if (old_down_cnt == mgp->down_cnt)
		printk(KERN_ERR "myri10ge: %s never got down irq\n", dev->name);

	netif_tx_disable(dev);

	myri10ge_free_rings(dev);

	mgp->running = MYRI10GE_ETH_STOPPED;
	return 0;
}

/* copy an array of struct mcp_kreq_ether_send's to the mcp.  Copy
 * backwards one at a time and handle ring wraps */

static inline void
myri10ge_submit_req_backwards(struct myri10ge_tx_buf *tx,
			      struct mcp_kreq_ether_send *src, int cnt)
{
	int idx, starting_slot;
	starting_slot = tx->req;
	while (cnt > 1) {
		cnt--;
		idx = (starting_slot + cnt) & tx->mask;
		myri10ge_pio_copy(&tx->lanai[idx], &src[cnt], sizeof(*src));
		mb();
	}
}

/*
 * copy an array of struct mcp_kreq_ether_send's to the mcp.  Copy
 * at most 32 bytes at a time, so as to avoid involving the software
 * pio handler in the nic.   We re-write the first segment's flags
 * to mark them valid only after writing the entire chain.
 */

static inline void
myri10ge_submit_req(struct myri10ge_tx_buf *tx, struct mcp_kreq_ether_send *src,
		    int cnt)
{
	int idx, i;
	struct mcp_kreq_ether_send __iomem *dstp, *dst;
	struct mcp_kreq_ether_send *srcp;
	u8 last_flags;

	idx = tx->req & tx->mask;

	last_flags = src->flags;
	src->flags = 0;
	mb();
	dst = dstp = &tx->lanai[idx];
	srcp = src;

	if ((idx + cnt) < tx->mask) {
		for (i = 0; i < (cnt - 1); i += 2) {
			myri10ge_pio_copy(dstp, srcp, 2 * sizeof(*src));
			mb();	/* force write every 32 bytes */
			srcp += 2;
			dstp += 2;
		}
	} else {
		/* submit all but the first request, and ensure
		 * that it is submitted below */
		myri10ge_submit_req_backwards(tx, src, cnt);
		i = 0;
	}
	if (i < cnt) {
		/* submit the first request */
		myri10ge_pio_copy(dstp, srcp, sizeof(*src));
		mb();		/* barrier before setting valid flag */
	}

	/* re-write the last 32-bits with the valid flags */
	src->flags = last_flags;
	put_be32(*((__be32 *) src + 3), (__be32 __iomem *) dst + 3);
	tx->req += cnt;
	mb();
}

static inline void
myri10ge_submit_req_wc(struct myri10ge_tx_buf *tx,
		       struct mcp_kreq_ether_send *src, int cnt)
{
	tx->req += cnt;
	mb();
	while (cnt >= 4) {
		myri10ge_pio_copy(tx->wc_fifo, src, 64);
		mb();
		src += 4;
		cnt -= 4;
	}
	if (cnt > 0) {
		/* pad it to 64 bytes.  The src is 64 bytes bigger than it
		 * needs to be so that we don't overrun it */
		myri10ge_pio_copy(tx->wc_fifo + MXGEFW_ETH_SEND_OFFSET(cnt),
				  src, 64);
		mb();
	}
}

/*
 * Transmit a packet.  We need to split the packet so that a single
 * segment does not cross myri10ge->tx.boundary, so this makes segment
 * counting tricky.  So rather than try to count segments up front, we
 * just give up if there are too few segments to hold a reasonably
 * fragmented packet currently available.  If we run
 * out of segments while preparing a packet for DMA, we just linearize
 * it and try again.
 */

static int myri10ge_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct myri10ge_priv *mgp = netdev_priv(dev);
	struct mcp_kreq_ether_send *req;
	struct myri10ge_tx_buf *tx = &mgp->tx;
	struct skb_frag_struct *frag;
	dma_addr_t bus;
	u32 low;
	__be32 high_swapped;
	unsigned int len;
	int idx, last_idx, avail, frag_cnt, frag_idx, count, mss, max_segments;
	u16 pseudo_hdr_offset, cksum_offset;
	int cum_len, seglen, boundary, rdma_count;
	u8 flags, odd_flag;

again:
	req = tx->req_list;
	avail = tx->mask - 1 - (tx->req - tx->done);

	mss = 0;
	max_segments = MXGEFW_MAX_SEND_DESC;

#ifdef NETIF_F_TSO
	if (skb->len > (dev->mtu + ETH_HLEN)) {
		mss = skb_shinfo(skb)->gso_size;
		if (mss != 0)
			max_segments = MYRI10GE_MAX_SEND_DESC_TSO;
	}
#endif				/*NETIF_F_TSO */

	if ((unlikely(avail < max_segments))) {
		/* we are out of transmit resources */
		mgp->stop_queue++;
		netif_stop_queue(dev);
		return 1;
	}

	/* Setup checksum offloading, if needed */
	cksum_offset = 0;
	pseudo_hdr_offset = 0;
	odd_flag = 0;
	flags = (MXGEFW_FLAGS_NO_TSO | MXGEFW_FLAGS_FIRST);
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		cksum_offset = (skb->h.raw - skb->data);
		pseudo_hdr_offset = cksum_offset + skb->csum_offset;
		/* If the headers are excessively large, then we must
		 * fall back to a software checksum */
		if (unlikely(cksum_offset > 255 || pseudo_hdr_offset > 127)) {
			if (skb_checksum_help(skb))
				goto drop;
			cksum_offset = 0;
			pseudo_hdr_offset = 0;
		} else {
			odd_flag = MXGEFW_FLAGS_ALIGN_ODD;
			flags |= MXGEFW_FLAGS_CKSUM;
		}
	}

	cum_len = 0;

#ifdef NETIF_F_TSO
	if (mss) {		/* TSO */
		/* this removes any CKSUM flag from before */
		flags = (MXGEFW_FLAGS_TSO_HDR | MXGEFW_FLAGS_FIRST);

		/* negative cum_len signifies to the
		 * send loop that we are still in the
		 * header portion of the TSO packet.
		 * TSO header must be at most 134 bytes long */
		cum_len = -((skb->h.raw - skb->data) + (skb->h.th->doff << 2));

		/* for TSO, pseudo_hdr_offset holds mss.
		 * The firmware figures out where to put
		 * the checksum by parsing the header. */
		pseudo_hdr_offset = mss;
	} else
#endif				/*NETIF_F_TSO */
		/* Mark small packets, and pad out tiny packets */
	if (skb->len <= MXGEFW_SEND_SMALL_SIZE) {
		flags |= MXGEFW_FLAGS_SMALL;

		/* pad frames to at least ETH_ZLEN bytes */
		if (unlikely(skb->len < ETH_ZLEN)) {
			if (skb_padto(skb, ETH_ZLEN)) {
				/* The packet is gone, so we must
				 * return 0 */
				mgp->stats.tx_dropped += 1;
				return 0;
			}
			/* adjust the len to account for the zero pad
			 * so that the nic can know how long it is */
			skb->len = ETH_ZLEN;
		}
	}

	/* map the skb for DMA */
	len = skb->len - skb->data_len;
	idx = tx->req & tx->mask;
	tx->info[idx].skb = skb;
	bus = pci_map_single(mgp->pdev, skb->data, len, PCI_DMA_TODEVICE);
	pci_unmap_addr_set(&tx->info[idx], bus, bus);
	pci_unmap_len_set(&tx->info[idx], len, len);

	frag_cnt = skb_shinfo(skb)->nr_frags;
	frag_idx = 0;
	count = 0;
	rdma_count = 0;

	/* "rdma_count" is the number of RDMAs belonging to the
	 * current packet BEFORE the current send request. For
	 * non-TSO packets, this is equal to "count".
	 * For TSO packets, rdma_count needs to be reset
	 * to 0 after a segment cut.
	 *
	 * The rdma_count field of the send request is
	 * the number of RDMAs of the packet starting at
	 * that request. For TSO send requests with one ore more cuts
	 * in the middle, this is the number of RDMAs starting
	 * after the last cut in the request. All previous
	 * segments before the last cut implicitly have 1 RDMA.
	 *
	 * Since the number of RDMAs is not known beforehand,
	 * it must be filled-in retroactively - after each
	 * segmentation cut or at the end of the entire packet.
	 */

	while (1) {
		/* Break the SKB or Fragment up into pieces which
		 * do not cross mgp->tx.boundary */
		low = MYRI10GE_LOWPART_TO_U32(bus);
		high_swapped = htonl(MYRI10GE_HIGHPART_TO_U32(bus));
		while (len) {
			u8 flags_next;
			int cum_len_next;

			if (unlikely(count == max_segments))
				goto abort_linearize;

			boundary = (low + tx->boundary) & ~(tx->boundary - 1);
			seglen = boundary - low;
			if (seglen > len)
				seglen = len;
			flags_next = flags & ~MXGEFW_FLAGS_FIRST;
			cum_len_next = cum_len + seglen;
#ifdef NETIF_F_TSO
			if (mss) {	/* TSO */
				(req - rdma_count)->rdma_count = rdma_count + 1;

				if (likely(cum_len >= 0)) {	/* payload */
					int next_is_first, chop;

					chop = (cum_len_next > mss);
					cum_len_next = cum_len_next % mss;
					next_is_first = (cum_len_next == 0);
					flags |= chop * MXGEFW_FLAGS_TSO_CHOP;
					flags_next |= next_is_first *
					    MXGEFW_FLAGS_FIRST;
					rdma_count |= -(chop | next_is_first);
					rdma_count += chop & !next_is_first;
				} else if (likely(cum_len_next >= 0)) {	/* header ends */
					int small;

					rdma_count = -1;
					cum_len_next = 0;
					seglen = -cum_len;
					small = (mss <= MXGEFW_SEND_SMALL_SIZE);
					flags_next = MXGEFW_FLAGS_TSO_PLD |
					    MXGEFW_FLAGS_FIRST |
					    (small * MXGEFW_FLAGS_SMALL);
				}
			}
#endif				/* NETIF_F_TSO */
			req->addr_high = high_swapped;
			req->addr_low = htonl(low);
			req->pseudo_hdr_offset = htons(pseudo_hdr_offset);
			req->pad = 0;	/* complete solid 16-byte block; does this matter? */
			req->rdma_count = 1;
			req->length = htons(seglen);
			req->cksum_offset = cksum_offset;
			req->flags = flags | ((cum_len & 1) * odd_flag);

			low += seglen;
			len -= seglen;
			cum_len = cum_len_next;
			flags = flags_next;
			req++;
			count++;
			rdma_count++;
			if (unlikely(cksum_offset > seglen))
				cksum_offset -= seglen;
			else
				cksum_offset = 0;
		}
		if (frag_idx == frag_cnt)
			break;

		/* map next fragment for DMA */
		idx = (count + tx->req) & tx->mask;
		frag = &skb_shinfo(skb)->frags[frag_idx];
		frag_idx++;
		len = frag->size;
		bus = pci_map_page(mgp->pdev, frag->page, frag->page_offset,
				   len, PCI_DMA_TODEVICE);
		pci_unmap_addr_set(&tx->info[idx], bus, bus);
		pci_unmap_len_set(&tx->info[idx], len, len);
	}

	(req - rdma_count)->rdma_count = rdma_count;
#ifdef NETIF_F_TSO
	if (mss)
		do {
			req--;
			req->flags |= MXGEFW_FLAGS_TSO_LAST;
		} while (!(req->flags & (MXGEFW_FLAGS_TSO_CHOP |
					 MXGEFW_FLAGS_FIRST)));
#endif
	idx = ((count - 1) + tx->req) & tx->mask;
	tx->info[idx].last = 1;
	if (tx->wc_fifo == NULL)
		myri10ge_submit_req(tx, tx->req_list, count);
	else
		myri10ge_submit_req_wc(tx, tx->req_list, count);
	tx->pkt_start++;
	if ((avail - count) < MXGEFW_MAX_SEND_DESC) {
		mgp->stop_queue++;
		netif_stop_queue(dev);
	}
	dev->trans_start = jiffies;
	return 0;

abort_linearize:
	/* Free any DMA resources we've alloced and clear out the skb
	 * slot so as to not trip up assertions, and to avoid a
	 * double-free if linearizing fails */

	last_idx = (idx + 1) & tx->mask;
	idx = tx->req & tx->mask;
	tx->info[idx].skb = NULL;
	do {
		len = pci_unmap_len(&tx->info[idx], len);
		if (len) {
			if (tx->info[idx].skb != NULL)
				pci_unmap_single(mgp->pdev,
						 pci_unmap_addr(&tx->info[idx],
								bus), len,
						 PCI_DMA_TODEVICE);
			else
				pci_unmap_page(mgp->pdev,
					       pci_unmap_addr(&tx->info[idx],
							      bus), len,
					       PCI_DMA_TODEVICE);
			pci_unmap_len_set(&tx->info[idx], len, 0);
			tx->info[idx].skb = NULL;
		}
		idx = (idx + 1) & tx->mask;
	} while (idx != last_idx);
	if (skb_is_gso(skb)) {
		printk(KERN_ERR
		       "myri10ge: %s: TSO but wanted to linearize?!?!?\n",
		       mgp->dev->name);
		goto drop;
	}

	if (skb_linearize(skb))
		goto drop;

	mgp->tx_linearized++;
	goto again;

drop:
	dev_kfree_skb_any(skb);
	mgp->stats.tx_dropped += 1;
	return 0;

}

static struct net_device_stats *myri10ge_get_stats(struct net_device *dev)
{
	struct myri10ge_priv *mgp = netdev_priv(dev);
	return &mgp->stats;
}

static void myri10ge_set_multicast_list(struct net_device *dev)
{
	struct myri10ge_cmd cmd;
	struct myri10ge_priv *mgp;
	struct dev_mc_list *mc_list;
	__be32 data[2] = { 0, 0 };
	int err;

	mgp = netdev_priv(dev);
	/* can be called from atomic contexts,
	 * pass 1 to force atomicity in myri10ge_send_cmd() */
	myri10ge_change_promisc(mgp, dev->flags & IFF_PROMISC, 1);

	/* This firmware is known to not support multicast */
	if (!mgp->fw_multicast_support)
		return;

	/* Disable multicast filtering */

	err = myri10ge_send_cmd(mgp, MXGEFW_ENABLE_ALLMULTI, &cmd, 1);
	if (err != 0) {
		printk(KERN_ERR "myri10ge: %s: Failed MXGEFW_ENABLE_ALLMULTI,"
		       " error status: %d\n", dev->name, err);
		goto abort;
	}

	if (dev->flags & IFF_ALLMULTI) {
		/* request to disable multicast filtering, so quit here */
		return;
	}

	/* Flush the filters */

	err = myri10ge_send_cmd(mgp, MXGEFW_LEAVE_ALL_MULTICAST_GROUPS,
				&cmd, 1);
	if (err != 0) {
		printk(KERN_ERR
		       "myri10ge: %s: Failed MXGEFW_LEAVE_ALL_MULTICAST_GROUPS"
		       ", error status: %d\n", dev->name, err);
		goto abort;
	}

	/* Walk the multicast list, and add each address */
	for (mc_list = dev->mc_list; mc_list != NULL; mc_list = mc_list->next) {
		memcpy(data, &mc_list->dmi_addr, 6);
		cmd.data0 = ntohl(data[0]);
		cmd.data1 = ntohl(data[1]);
		err = myri10ge_send_cmd(mgp, MXGEFW_JOIN_MULTICAST_GROUP,
					&cmd, 1);

		if (err != 0) {
			printk(KERN_ERR "myri10ge: %s: Failed "
			       "MXGEFW_JOIN_MULTICAST_GROUP, error status:"
			       "%d\t", dev->name, err);
			printk(KERN_ERR "MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
			       ((unsigned char *)&mc_list->dmi_addr)[0],
			       ((unsigned char *)&mc_list->dmi_addr)[1],
			       ((unsigned char *)&mc_list->dmi_addr)[2],
			       ((unsigned char *)&mc_list->dmi_addr)[3],
			       ((unsigned char *)&mc_list->dmi_addr)[4],
			       ((unsigned char *)&mc_list->dmi_addr)[5]
			    );
			goto abort;
		}
	}
	/* Enable multicast filtering */
	err = myri10ge_send_cmd(mgp, MXGEFW_DISABLE_ALLMULTI, &cmd, 1);
	if (err != 0) {
		printk(KERN_ERR "myri10ge: %s: Failed MXGEFW_DISABLE_ALLMULTI,"
		       "error status: %d\n", dev->name, err);
		goto abort;
	}

	return;

abort:
	return;
}

static int myri10ge_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;
	struct myri10ge_priv *mgp = netdev_priv(dev);
	int status;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	status = myri10ge_update_mac_address(mgp, sa->sa_data);
	if (status != 0) {
		printk(KERN_ERR
		       "myri10ge: %s: changing mac address failed with %d\n",
		       dev->name, status);
		return status;
	}

	/* change the dev structure */
	memcpy(dev->dev_addr, sa->sa_data, 6);
	return 0;
}

static int myri10ge_change_mtu(struct net_device *dev, int new_mtu)
{
	struct myri10ge_priv *mgp = netdev_priv(dev);
	int error = 0;

	if ((new_mtu < 68) || (ETH_HLEN + new_mtu > MYRI10GE_MAX_ETHER_MTU)) {
		printk(KERN_ERR "myri10ge: %s: new mtu (%d) is not valid\n",
		       dev->name, new_mtu);
		return -EINVAL;
	}
	printk(KERN_INFO "%s: changing mtu from %d to %d\n",
	       dev->name, dev->mtu, new_mtu);
	if (mgp->running) {
		/* if we change the mtu on an active device, we must
		 * reset the device so the firmware sees the change */
		myri10ge_close(dev);
		dev->mtu = new_mtu;
		myri10ge_open(dev);
	} else
		dev->mtu = new_mtu;

	return error;
}

/*
 * Enable ECRC to align PCI-E Completion packets on an 8-byte boundary.
 * Only do it if the bridge is a root port since we don't want to disturb
 * any other device, except if forced with myri10ge_ecrc_enable > 1.
 */

static void myri10ge_enable_ecrc(struct myri10ge_priv *mgp)
{
	struct pci_dev *bridge = mgp->pdev->bus->self;
	struct device *dev = &mgp->pdev->dev;
	unsigned cap;
	unsigned err_cap;
	u16 val;
	u8 ext_type;
	int ret;

	if (!myri10ge_ecrc_enable || !bridge)
		return;

	/* check that the bridge is a root port */
	cap = pci_find_capability(bridge, PCI_CAP_ID_EXP);
	pci_read_config_word(bridge, cap + PCI_CAP_FLAGS, &val);
	ext_type = (val & PCI_EXP_FLAGS_TYPE) >> 4;
	if (ext_type != PCI_EXP_TYPE_ROOT_PORT) {
		if (myri10ge_ecrc_enable > 1) {
			struct pci_dev *old_bridge = bridge;

			/* Walk the hierarchy up to the root port
			 * where ECRC has to be enabled */
			do {
				bridge = bridge->bus->self;
				if (!bridge) {
					dev_err(dev,
						"Failed to find root port"
						" to force ECRC\n");
					return;
				}
				cap =
				    pci_find_capability(bridge, PCI_CAP_ID_EXP);
				pci_read_config_word(bridge,
						     cap + PCI_CAP_FLAGS, &val);
				ext_type = (val & PCI_EXP_FLAGS_TYPE) >> 4;
			} while (ext_type != PCI_EXP_TYPE_ROOT_PORT);

			dev_info(dev,
				 "Forcing ECRC on non-root port %s"
				 " (enabling on root port %s)\n",
				 pci_name(old_bridge), pci_name(bridge));
		} else {
			dev_err(dev,
				"Not enabling ECRC on non-root port %s\n",
				pci_name(bridge));
			return;
		}
	}

	cap = pci_find_ext_capability(bridge, PCI_EXT_CAP_ID_ERR);
	if (!cap)
		return;

	ret = pci_read_config_dword(bridge, cap + PCI_ERR_CAP, &err_cap);
	if (ret) {
		dev_err(dev, "failed reading ext-conf-space of %s\n",
			pci_name(bridge));
		dev_err(dev, "\t pci=nommconf in use? "
			"or buggy/incomplete/absent ACPI MCFG attr?\n");
		return;
	}
	if (!(err_cap & PCI_ERR_CAP_ECRC_GENC))
		return;

	err_cap |= PCI_ERR_CAP_ECRC_GENE;
	pci_write_config_dword(bridge, cap + PCI_ERR_CAP, err_cap);
	dev_info(dev, "Enabled ECRC on upstream bridge %s\n", pci_name(bridge));
	mgp->tx.boundary = 4096;
	mgp->fw_name = myri10ge_fw_aligned;
}

/*
 * The Lanai Z8E PCI-E interface achieves higher Read-DMA throughput
 * when the PCI-E Completion packets are aligned on an 8-byte
 * boundary.  Some PCI-E chip sets always align Completion packets; on
 * the ones that do not, the alignment can be enforced by enabling
 * ECRC generation (if supported).
 *
 * When PCI-E Completion packets are not aligned, it is actually more
 * efficient to limit Read-DMA transactions to 2KB, rather than 4KB.
 *
 * If the driver can neither enable ECRC nor verify that it has
 * already been enabled, then it must use a firmware image which works
 * around unaligned completion packets (myri10ge_ethp_z8e.dat), and it
 * should also ensure that it never gives the device a Read-DMA which is
 * larger than 2KB by setting the tx.boundary to 2KB.  If ECRC is
 * enabled, then the driver should use the aligned (myri10ge_eth_z8e.dat)
 * firmware image, and set tx.boundary to 4KB.
 */

#define PCI_DEVICE_ID_INTEL_E5000_PCIE23 0x25f7
#define PCI_DEVICE_ID_INTEL_E5000_PCIE47 0x25fa

static void myri10ge_select_firmware(struct myri10ge_priv *mgp)
{
	struct pci_dev *bridge = mgp->pdev->bus->self;

	mgp->tx.boundary = 2048;
	mgp->fw_name = myri10ge_fw_unaligned;

	if (myri10ge_force_firmware == 0) {
		int link_width, exp_cap;
		u16 lnk;

		exp_cap = pci_find_capability(mgp->pdev, PCI_CAP_ID_EXP);
		pci_read_config_word(mgp->pdev, exp_cap + PCI_EXP_LNKSTA, &lnk);
		link_width = (lnk >> 4) & 0x3f;

		myri10ge_enable_ecrc(mgp);

		/* Check to see if Link is less than 8 or if the
		 * upstream bridge is known to provide aligned
		 * completions */
		if (link_width < 8) {
			dev_info(&mgp->pdev->dev, "PCIE x%d Link\n",
				 link_width);
			mgp->tx.boundary = 4096;
			mgp->fw_name = myri10ge_fw_aligned;
		} else if (bridge &&
			   /* ServerWorks HT2000/HT1000 */
			   ((bridge->vendor == PCI_VENDOR_ID_SERVERWORKS
			     && bridge->device ==
			     PCI_DEVICE_ID_SERVERWORKS_HT2000_PCIE)
			    /* All Intel E5000 PCIE ports */
			    || (bridge->vendor == PCI_VENDOR_ID_INTEL
				&& bridge->device >=
				PCI_DEVICE_ID_INTEL_E5000_PCIE23
				&& bridge->device <=
				PCI_DEVICE_ID_INTEL_E5000_PCIE47))) {
			dev_info(&mgp->pdev->dev,
				 "Assuming aligned completions (0x%x:0x%x)\n",
				 bridge->vendor, bridge->device);
			mgp->tx.boundary = 4096;
			mgp->fw_name = myri10ge_fw_aligned;
		}
	} else {
		if (myri10ge_force_firmware == 1) {
			dev_info(&mgp->pdev->dev,
				 "Assuming aligned completions (forced)\n");
			mgp->tx.boundary = 4096;
			mgp->fw_name = myri10ge_fw_aligned;
		} else {
			dev_info(&mgp->pdev->dev,
				 "Assuming unaligned completions (forced)\n");
			mgp->tx.boundary = 2048;
			mgp->fw_name = myri10ge_fw_unaligned;
		}
	}
	if (myri10ge_fw_name != NULL) {
		dev_info(&mgp->pdev->dev, "overriding firmware to %s\n",
			 myri10ge_fw_name);
		mgp->fw_name = myri10ge_fw_name;
	}
}

static void myri10ge_save_state(struct myri10ge_priv *mgp)
{
	struct pci_dev *pdev = mgp->pdev;
	int cap;

	pci_save_state(pdev);
	/* now save PCIe and MSI state that Linux will not
	 * save for us */
	cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_dword(pdev, cap + PCI_EXP_DEVCTL, &mgp->devctl);
	cap = pci_find_capability(pdev, PCI_CAP_ID_MSI);
	pci_read_config_word(pdev, cap + PCI_MSI_FLAGS, &mgp->msi_flags);
}

static void myri10ge_restore_state(struct myri10ge_priv *mgp)
{
	struct pci_dev *pdev = mgp->pdev;
	int cap;

	/* restore PCIe and MSI state that linux will not */
	cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_write_config_dword(pdev, cap + PCI_CAP_ID_EXP, mgp->devctl);
	cap = pci_find_capability(pdev, PCI_CAP_ID_MSI);
	pci_write_config_word(pdev, cap + PCI_MSI_FLAGS, mgp->msi_flags);

	pci_restore_state(pdev);
}

#ifdef CONFIG_PM

static int myri10ge_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct myri10ge_priv *mgp;
	struct net_device *netdev;

	mgp = pci_get_drvdata(pdev);
	if (mgp == NULL)
		return -EINVAL;
	netdev = mgp->dev;

	netif_device_detach(netdev);
	if (netif_running(netdev)) {
		printk(KERN_INFO "myri10ge: closing %s\n", netdev->name);
		rtnl_lock();
		myri10ge_close(netdev);
		rtnl_unlock();
	}
	myri10ge_dummy_rdma(mgp, 0);
	free_irq(pdev->irq, mgp);
	myri10ge_save_state(mgp);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int myri10ge_resume(struct pci_dev *pdev)
{
	struct myri10ge_priv *mgp;
	struct net_device *netdev;
	int status;
	u16 vendor;

	mgp = pci_get_drvdata(pdev);
	if (mgp == NULL)
		return -EINVAL;
	netdev = mgp->dev;
	pci_set_power_state(pdev, 0);	/* zeros conf space as a side effect */
	msleep(5);		/* give card time to respond */
	pci_read_config_word(mgp->pdev, PCI_VENDOR_ID, &vendor);
	if (vendor == 0xffff) {
		printk(KERN_ERR "myri10ge: %s: device disappeared!\n",
		       mgp->dev->name);
		return -EIO;
	}
	myri10ge_restore_state(mgp);

	status = pci_enable_device(pdev);
	if (status < 0) {
		dev_err(&pdev->dev, "failed to enable device\n");
		return -EIO;
	}

	pci_set_master(pdev);

	status = request_irq(pdev->irq, myri10ge_intr, IRQF_SHARED,
			     netdev->name, mgp);
	if (status != 0) {
		dev_err(&pdev->dev, "failed to allocate IRQ\n");
		goto abort_with_enabled;
	}

	myri10ge_reset(mgp);
	myri10ge_dummy_rdma(mgp, 1);

	/* Save configuration space to be restored if the
	 * nic resets due to a parity error */
	myri10ge_save_state(mgp);

	if (netif_running(netdev)) {
		rtnl_lock();
		myri10ge_open(netdev);
		rtnl_unlock();
	}
	netif_device_attach(netdev);

	return 0;

abort_with_enabled:
	pci_disable_device(pdev);
	return -EIO;

}

#endif				/* CONFIG_PM */

static u32 myri10ge_read_reboot(struct myri10ge_priv *mgp)
{
	struct pci_dev *pdev = mgp->pdev;
	int vs = mgp->vendor_specific_offset;
	u32 reboot;

	/*enter read32 mode */
	pci_write_config_byte(pdev, vs + 0x10, 0x3);

	/*read REBOOT_STATUS (0xfffffff0) */
	pci_write_config_dword(pdev, vs + 0x18, 0xfffffff0);
	pci_read_config_dword(pdev, vs + 0x14, &reboot);
	return reboot;
}

/*
 * This watchdog is used to check whether the board has suffered
 * from a parity error and needs to be recovered.
 */
static void myri10ge_watchdog(struct work_struct *work)
{
	struct myri10ge_priv *mgp =
	    container_of(work, struct myri10ge_priv, watchdog_work);
	u32 reboot;
	int status;
	u16 cmd, vendor;

	mgp->watchdog_resets++;
	pci_read_config_word(mgp->pdev, PCI_COMMAND, &cmd);
	if ((cmd & PCI_COMMAND_MASTER) == 0) {
		/* Bus master DMA disabled?  Check to see
		 * if the card rebooted due to a parity error
		 * For now, just report it */
		reboot = myri10ge_read_reboot(mgp);
		printk(KERN_ERR
		       "myri10ge: %s: NIC rebooted (0x%x), resetting\n",
		       mgp->dev->name, reboot);
		/*
		 * A rebooted nic will come back with config space as
		 * it was after power was applied to PCIe bus.
		 * Attempt to restore config space which was saved
		 * when the driver was loaded, or the last time the
		 * nic was resumed from power saving mode.
		 */
		myri10ge_restore_state(mgp);
	} else {
		/* if we get back -1's from our slot, perhaps somebody
		 * powered off our card.  Don't try to reset it in
		 * this case */
		if (cmd == 0xffff) {
			pci_read_config_word(mgp->pdev, PCI_VENDOR_ID, &vendor);
			if (vendor == 0xffff) {
				printk(KERN_ERR
				       "myri10ge: %s: device disappeared!\n",
				       mgp->dev->name);
				return;
			}
		}
		/* Perhaps it is a software error.  Try to reset */

		printk(KERN_ERR "myri10ge: %s: device timeout, resetting\n",
		       mgp->dev->name);
		printk(KERN_INFO "myri10ge: %s: %d %d %d %d %d\n",
		       mgp->dev->name, mgp->tx.req, mgp->tx.done,
		       mgp->tx.pkt_start, mgp->tx.pkt_done,
		       (int)ntohl(mgp->fw_stats->send_done_count));
		msleep(2000);
		printk(KERN_INFO "myri10ge: %s: %d %d %d %d %d\n",
		       mgp->dev->name, mgp->tx.req, mgp->tx.done,
		       mgp->tx.pkt_start, mgp->tx.pkt_done,
		       (int)ntohl(mgp->fw_stats->send_done_count));
	}
	rtnl_lock();
	myri10ge_close(mgp->dev);
	status = myri10ge_load_firmware(mgp);
	if (status != 0)
		printk(KERN_ERR "myri10ge: %s: failed to load firmware\n",
		       mgp->dev->name);
	else
		myri10ge_open(mgp->dev);
	rtnl_unlock();
}

/*
 * We use our own timer routine rather than relying upon
 * netdev->tx_timeout because we have a very large hardware transmit
 * queue.  Due to the large queue, the netdev->tx_timeout function
 * cannot detect a NIC with a parity error in a timely fashion if the
 * NIC is lightly loaded.
 */
static void myri10ge_watchdog_timer(unsigned long arg)
{
	struct myri10ge_priv *mgp;

	mgp = (struct myri10ge_priv *)arg;
	if (mgp->tx.req != mgp->tx.done &&
	    mgp->tx.done == mgp->watchdog_tx_done &&
	    mgp->watchdog_tx_req != mgp->watchdog_tx_done)
		/* nic seems like it might be stuck.. */
		schedule_work(&mgp->watchdog_work);
	else
		/* rearm timer */
		mod_timer(&mgp->watchdog_timer,
			  jiffies + myri10ge_watchdog_timeout * HZ);

	mgp->watchdog_tx_done = mgp->tx.done;
	mgp->watchdog_tx_req = mgp->tx.req;
}

static int myri10ge_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct myri10ge_priv *mgp;
	struct device *dev = &pdev->dev;
	size_t bytes;
	int i;
	int status = -ENXIO;
	int cap;
	int dac_enabled;
	u16 val;

	netdev = alloc_etherdev(sizeof(*mgp));
	if (netdev == NULL) {
		dev_err(dev, "Could not allocate ethernet device\n");
		return -ENOMEM;
	}

	mgp = netdev_priv(netdev);
	memset(mgp, 0, sizeof(*mgp));
	mgp->dev = netdev;
	mgp->pdev = pdev;
	mgp->csum_flag = MXGEFW_FLAGS_CKSUM;
	mgp->pause = myri10ge_flow_control;
	mgp->intr_coal_delay = myri10ge_intr_coal_delay;
	mgp->msg_enable = netif_msg_init(myri10ge_debug, MYRI10GE_MSG_DEFAULT);
	init_waitqueue_head(&mgp->down_wq);

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev, "pci_enable_device call failed\n");
		status = -ENODEV;
		goto abort_with_netdev;
	}
	myri10ge_select_firmware(mgp);

	/* Find the vendor-specific cap so we can check
	 * the reboot register later on */
	mgp->vendor_specific_offset
	    = pci_find_capability(pdev, PCI_CAP_ID_VNDR);

	/* Set our max read request to 4KB */
	cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (cap < 64) {
		dev_err(&pdev->dev, "Bad PCI_CAP_ID_EXP location %d\n", cap);
		goto abort_with_netdev;
	}
	status = pci_read_config_word(pdev, cap + PCI_EXP_DEVCTL, &val);
	if (status != 0) {
		dev_err(&pdev->dev, "Error %d reading PCI_EXP_DEVCTL\n",
			status);
		goto abort_with_netdev;
	}
	val = (val & ~PCI_EXP_DEVCTL_READRQ) | (5 << 12);
	status = pci_write_config_word(pdev, cap + PCI_EXP_DEVCTL, val);
	if (status != 0) {
		dev_err(&pdev->dev, "Error %d writing PCI_EXP_DEVCTL\n",
			status);
		goto abort_with_netdev;
	}

	pci_set_master(pdev);
	dac_enabled = 1;
	status = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
	if (status != 0) {
		dac_enabled = 0;
		dev_err(&pdev->dev,
			"64-bit pci address mask was refused, trying 32-bit");
		status = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	}
	if (status != 0) {
		dev_err(&pdev->dev, "Error %d setting DMA mask\n", status);
		goto abort_with_netdev;
	}
	mgp->cmd = dma_alloc_coherent(&pdev->dev, sizeof(*mgp->cmd),
				      &mgp->cmd_bus, GFP_KERNEL);
	if (mgp->cmd == NULL)
		goto abort_with_netdev;

	mgp->fw_stats = dma_alloc_coherent(&pdev->dev, sizeof(*mgp->fw_stats),
					   &mgp->fw_stats_bus, GFP_KERNEL);
	if (mgp->fw_stats == NULL)
		goto abort_with_cmd;

	mgp->board_span = pci_resource_len(pdev, 0);
	mgp->iomem_base = pci_resource_start(pdev, 0);
	mgp->mtrr = -1;
#ifdef CONFIG_MTRR
	mgp->mtrr = mtrr_add(mgp->iomem_base, mgp->board_span,
			     MTRR_TYPE_WRCOMB, 1);
#endif
	/* Hack.  need to get rid of these magic numbers */
	mgp->sram_size =
	    2 * 1024 * 1024 - (2 * (48 * 1024) + (32 * 1024)) - 0x100;
	if (mgp->sram_size > mgp->board_span) {
		dev_err(&pdev->dev, "board span %ld bytes too small\n",
			mgp->board_span);
		goto abort_with_wc;
	}
	mgp->sram = ioremap(mgp->iomem_base, mgp->board_span);
	if (mgp->sram == NULL) {
		dev_err(&pdev->dev, "ioremap failed for %ld bytes at 0x%lx\n",
			mgp->board_span, mgp->iomem_base);
		status = -ENXIO;
		goto abort_with_wc;
	}
	memcpy_fromio(mgp->eeprom_strings,
		      mgp->sram + mgp->sram_size - MYRI10GE_EEPROM_STRINGS_SIZE,
		      MYRI10GE_EEPROM_STRINGS_SIZE);
	memset(mgp->eeprom_strings + MYRI10GE_EEPROM_STRINGS_SIZE - 2, 0, 2);
	status = myri10ge_read_mac_addr(mgp);
	if (status)
		goto abort_with_ioremap;

	for (i = 0; i < ETH_ALEN; i++)
		netdev->dev_addr[i] = mgp->mac_addr[i];

	/* allocate rx done ring */
	bytes = myri10ge_max_intr_slots * sizeof(*mgp->rx_done.entry);
	mgp->rx_done.entry = dma_alloc_coherent(&pdev->dev, bytes,
						&mgp->rx_done.bus, GFP_KERNEL);
	if (mgp->rx_done.entry == NULL)
		goto abort_with_ioremap;
	memset(mgp->rx_done.entry, 0, bytes);

	status = myri10ge_load_firmware(mgp);
	if (status != 0) {
		dev_err(&pdev->dev, "failed to load firmware\n");
		goto abort_with_rx_done;
	}

	status = myri10ge_reset(mgp);
	if (status != 0) {
		dev_err(&pdev->dev, "failed reset\n");
		goto abort_with_firmware;
	}

	if (myri10ge_msi) {
		status = pci_enable_msi(pdev);
		if (status != 0)
			dev_err(&pdev->dev,
				"Error %d setting up MSI; falling back to xPIC\n",
				status);
		else
			mgp->msi_enabled = 1;
	}

	status = request_irq(pdev->irq, myri10ge_intr, IRQF_SHARED,
			     netdev->name, mgp);
	if (status != 0) {
		dev_err(&pdev->dev, "failed to allocate IRQ\n");
		goto abort_with_firmware;
	}

	pci_set_drvdata(pdev, mgp);
	if ((myri10ge_initial_mtu + ETH_HLEN) > MYRI10GE_MAX_ETHER_MTU)
		myri10ge_initial_mtu = MYRI10GE_MAX_ETHER_MTU - ETH_HLEN;
	if ((myri10ge_initial_mtu + ETH_HLEN) < 68)
		myri10ge_initial_mtu = 68;
	netdev->mtu = myri10ge_initial_mtu;
	netdev->open = myri10ge_open;
	netdev->stop = myri10ge_close;
	netdev->hard_start_xmit = myri10ge_xmit;
	netdev->get_stats = myri10ge_get_stats;
	netdev->base_addr = mgp->iomem_base;
	netdev->irq = pdev->irq;
	netdev->change_mtu = myri10ge_change_mtu;
	netdev->set_multicast_list = myri10ge_set_multicast_list;
	netdev->set_mac_address = myri10ge_set_mac_address;
	netdev->features = NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_TSO;
	if (dac_enabled)
		netdev->features |= NETIF_F_HIGHDMA;
	netdev->poll = myri10ge_poll;
	netdev->weight = myri10ge_napi_weight;

	/* Save configuration space to be restored if the
	 * nic resets due to a parity error */
	myri10ge_save_state(mgp);

	/* Setup the watchdog timer */
	setup_timer(&mgp->watchdog_timer, myri10ge_watchdog_timer,
		    (unsigned long)mgp);

	SET_ETHTOOL_OPS(netdev, &myri10ge_ethtool_ops);
	INIT_WORK(&mgp->watchdog_work, myri10ge_watchdog);
	status = register_netdev(netdev);
	if (status != 0) {
		dev_err(&pdev->dev, "register_netdev failed: %d\n", status);
		goto abort_with_irq;
	}
	dev_info(dev, "%s IRQ %d, tx bndry %d, fw %s, WC %s\n",
		 (mgp->msi_enabled ? "MSI" : "xPIC"),
		 pdev->irq, mgp->tx.boundary, mgp->fw_name,
		 (mgp->mtrr >= 0 ? "Enabled" : "Disabled"));

	return 0;

abort_with_irq:
	free_irq(pdev->irq, mgp);
	if (mgp->msi_enabled)
		pci_disable_msi(pdev);

abort_with_firmware:
	myri10ge_dummy_rdma(mgp, 0);

abort_with_rx_done:
	bytes = myri10ge_max_intr_slots * sizeof(*mgp->rx_done.entry);
	dma_free_coherent(&pdev->dev, bytes,
			  mgp->rx_done.entry, mgp->rx_done.bus);

abort_with_ioremap:
	iounmap(mgp->sram);

abort_with_wc:
#ifdef CONFIG_MTRR
	if (mgp->mtrr >= 0)
		mtrr_del(mgp->mtrr, mgp->iomem_base, mgp->board_span);
#endif
	dma_free_coherent(&pdev->dev, sizeof(*mgp->fw_stats),
			  mgp->fw_stats, mgp->fw_stats_bus);

abort_with_cmd:
	dma_free_coherent(&pdev->dev, sizeof(*mgp->cmd),
			  mgp->cmd, mgp->cmd_bus);

abort_with_netdev:

	free_netdev(netdev);
	return status;
}

/*
 * myri10ge_remove
 *
 * Does what is necessary to shutdown one Myrinet device. Called
 *   once for each Myrinet card by the kernel when a module is
 *   unloaded.
 */
static void myri10ge_remove(struct pci_dev *pdev)
{
	struct myri10ge_priv *mgp;
	struct net_device *netdev;
	size_t bytes;

	mgp = pci_get_drvdata(pdev);
	if (mgp == NULL)
		return;

	flush_scheduled_work();
	netdev = mgp->dev;
	unregister_netdev(netdev);
	free_irq(pdev->irq, mgp);
	if (mgp->msi_enabled)
		pci_disable_msi(pdev);

	myri10ge_dummy_rdma(mgp, 0);

	bytes = myri10ge_max_intr_slots * sizeof(*mgp->rx_done.entry);
	dma_free_coherent(&pdev->dev, bytes,
			  mgp->rx_done.entry, mgp->rx_done.bus);

	iounmap(mgp->sram);

#ifdef CONFIG_MTRR
	if (mgp->mtrr >= 0)
		mtrr_del(mgp->mtrr, mgp->iomem_base, mgp->board_span);
#endif
	dma_free_coherent(&pdev->dev, sizeof(*mgp->fw_stats),
			  mgp->fw_stats, mgp->fw_stats_bus);

	dma_free_coherent(&pdev->dev, sizeof(*mgp->cmd),
			  mgp->cmd, mgp->cmd_bus);

	free_netdev(netdev);
	pci_set_drvdata(pdev, NULL);
}

#define PCI_DEVICE_ID_MYRICOM_MYRI10GE_Z8E 	0x0008

static struct pci_device_id myri10ge_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_MYRICOM, PCI_DEVICE_ID_MYRICOM_MYRI10GE_Z8E)},
	{0},
};

static struct pci_driver myri10ge_driver = {
	.name = "myri10ge",
	.probe = myri10ge_probe,
	.remove = myri10ge_remove,
	.id_table = myri10ge_pci_tbl,
#ifdef CONFIG_PM
	.suspend = myri10ge_suspend,
	.resume = myri10ge_resume,
#endif
};

static __init int myri10ge_init_module(void)
{
	printk(KERN_INFO "%s: Version %s\n", myri10ge_driver.name,
	       MYRI10GE_VERSION_STR);
	return pci_register_driver(&myri10ge_driver);
}

module_init(myri10ge_init_module);

static __exit void myri10ge_cleanup_module(void)
{
	pci_unregister_driver(&myri10ge_driver);
}

module_exit(myri10ge_cleanup_module);
