/*
 *  smctr.c: A network driver for the SMC Token Ring Adapters.
 *
 *  Written by Jay Schulist <jschlst@samba.org>
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 *
 *  This device driver works with the following SMC adapters:
 *      - SMC TokenCard Elite   (8115T, chips 825/584)
 *      - SMC TokenCard Elite/A MCA (8115T/A, chips 825/594)
 *
 *  Source(s):
 *  	- SMC TokenCard SDK.
 *
 *  Maintainer(s):
 *    JS        Jay Schulist <jschlst@samba.org>
 *
 * Changes:
 *    07102000          JS      Fixed a timing problem in smctr_wait_cmd();
 *                              Also added a bit more discriptive error msgs.
 *    07122000          JS      Fixed problem with detecting a card with
 *				module io/irq/mem specified.
 *
 *  To do:
 *    1. Multicast support.
 *
 *  Initial 2.5 cleanup Alan Cox <alan@lxorguk.ukuu.org.uk>  2002/10/28
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mca-legacy.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/trdevice.h>
#include <linux/bitops.h>
#include <linux/firmware.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>

#if BITS_PER_LONG == 64
#error FIXME: driver does not support 64-bit platforms
#endif

#include "smctr.h"               /* Our Stuff */

static const char version[] __initdata =
	KERN_INFO "smctr.c: v1.4 7/12/00 by jschlst@samba.org\n";
static const char cardname[] = "smctr";


#define SMCTR_IO_EXTENT   20

#ifdef CONFIG_MCA_LEGACY
static unsigned int smctr_posid = 0x6ec6;
#endif

static int ringspeed;

/* SMC Name of the Adapter. */
static char smctr_name[] = "SMC TokenCard";
static char *smctr_model = "Unknown";

/* Use 0 for production, 1 for verification, 2 for debug, and
 * 3 for very verbose debug.
 */
#ifndef SMCTR_DEBUG
#define SMCTR_DEBUG 1
#endif
static unsigned int smctr_debug = SMCTR_DEBUG;

/* smctr.c prototypes and functions are arranged alphabeticly 
 * for clearity, maintainability and pure old fashion fun. 
 */
/* A */
static int smctr_alloc_shared_memory(struct net_device *dev);

/* B */
static int smctr_bypass_state(struct net_device *dev);

/* C */
static int smctr_checksum_firmware(struct net_device *dev);
static int __init smctr_chk_isa(struct net_device *dev);
static int smctr_chg_rx_mask(struct net_device *dev);
static int smctr_clear_int(struct net_device *dev);
static int smctr_clear_trc_reset(int ioaddr);
static int smctr_close(struct net_device *dev);

/* D */
static int smctr_decode_firmware(struct net_device *dev,
				 const struct firmware *fw);
static int smctr_disable_16bit(struct net_device *dev);
static int smctr_disable_adapter_ctrl_store(struct net_device *dev);
static int smctr_disable_bic_int(struct net_device *dev);

/* E */
static int smctr_enable_16bit(struct net_device *dev);
static int smctr_enable_adapter_ctrl_store(struct net_device *dev);
static int smctr_enable_adapter_ram(struct net_device *dev);
static int smctr_enable_bic_int(struct net_device *dev);

/* G */
static int __init smctr_get_boardid(struct net_device *dev, int mca);
static int smctr_get_group_address(struct net_device *dev);
static int smctr_get_functional_address(struct net_device *dev);
static unsigned int smctr_get_num_rx_bdbs(struct net_device *dev);
static int smctr_get_physical_drop_number(struct net_device *dev);
static __u8 *smctr_get_rx_pointer(struct net_device *dev, short queue);
static int smctr_get_station_id(struct net_device *dev);
static FCBlock *smctr_get_tx_fcb(struct net_device *dev, __u16 queue,
        __u16 bytes_count);
static int smctr_get_upstream_neighbor_addr(struct net_device *dev);

/* H */
static int smctr_hardware_send_packet(struct net_device *dev,
        struct net_local *tp);
/* I */
static int smctr_init_acbs(struct net_device *dev);
static int smctr_init_adapter(struct net_device *dev);
static int smctr_init_card_real(struct net_device *dev);
static int smctr_init_rx_bdbs(struct net_device *dev);
static int smctr_init_rx_fcbs(struct net_device *dev);
static int smctr_init_shared_memory(struct net_device *dev);
static int smctr_init_tx_bdbs(struct net_device *dev);
static int smctr_init_tx_fcbs(struct net_device *dev);
static int smctr_internal_self_test(struct net_device *dev);
static irqreturn_t smctr_interrupt(int irq, void *dev_id);
static int smctr_issue_enable_int_cmd(struct net_device *dev,
        __u16 interrupt_enable_mask);
static int smctr_issue_int_ack(struct net_device *dev, __u16 iack_code,
        __u16 ibits);
static int smctr_issue_init_timers_cmd(struct net_device *dev);
static int smctr_issue_init_txrx_cmd(struct net_device *dev);
static int smctr_issue_insert_cmd(struct net_device *dev);
static int smctr_issue_read_ring_status_cmd(struct net_device *dev);
static int smctr_issue_read_word_cmd(struct net_device *dev, __u16 aword_cnt);
static int smctr_issue_remove_cmd(struct net_device *dev);
static int smctr_issue_resume_acb_cmd(struct net_device *dev);
static int smctr_issue_resume_rx_bdb_cmd(struct net_device *dev, __u16 queue);
static int smctr_issue_resume_rx_fcb_cmd(struct net_device *dev, __u16 queue);
static int smctr_issue_resume_tx_fcb_cmd(struct net_device *dev, __u16 queue);
static int smctr_issue_test_internal_rom_cmd(struct net_device *dev);
static int smctr_issue_test_hic_cmd(struct net_device *dev);
static int smctr_issue_test_mac_reg_cmd(struct net_device *dev);
static int smctr_issue_trc_loopback_cmd(struct net_device *dev);
static int smctr_issue_tri_loopback_cmd(struct net_device *dev);
static int smctr_issue_write_byte_cmd(struct net_device *dev,
        short aword_cnt, void *byte);
static int smctr_issue_write_word_cmd(struct net_device *dev,
        short aword_cnt, void *word);

/* J */
static int smctr_join_complete_state(struct net_device *dev);

/* L */
static int smctr_link_tx_fcbs_to_bdbs(struct net_device *dev);
static int smctr_load_firmware(struct net_device *dev);
static int smctr_load_node_addr(struct net_device *dev);
static int smctr_lobe_media_test(struct net_device *dev);
static int smctr_lobe_media_test_cmd(struct net_device *dev);
static int smctr_lobe_media_test_state(struct net_device *dev);

/* M */
static int smctr_make_8025_hdr(struct net_device *dev,
        MAC_HEADER *rmf, MAC_HEADER *tmf, __u16 ac_fc);
static int smctr_make_access_pri(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);
static int smctr_make_addr_mod(struct net_device *dev, MAC_SUB_VECTOR *tsv);
static int smctr_make_auth_funct_class(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);
static int smctr_make_corr(struct net_device *dev,
        MAC_SUB_VECTOR *tsv, __u16 correlator);
static int smctr_make_funct_addr(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);
static int smctr_make_group_addr(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);
static int smctr_make_phy_drop_num(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);
static int smctr_make_product_id(struct net_device *dev, MAC_SUB_VECTOR *tsv);
static int smctr_make_station_id(struct net_device *dev, MAC_SUB_VECTOR *tsv);
static int smctr_make_ring_station_status(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);
static int smctr_make_ring_station_version(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);
static int smctr_make_tx_status_code(struct net_device *dev,
        MAC_SUB_VECTOR *tsv, __u16 tx_fstatus);
static int smctr_make_upstream_neighbor_addr(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);
static int smctr_make_wrap_data(struct net_device *dev,
        MAC_SUB_VECTOR *tsv);

/* O */
static int smctr_open(struct net_device *dev);
static int smctr_open_tr(struct net_device *dev);

/* P */
struct net_device *smctr_probe(int unit);
static int __init smctr_probe1(struct net_device *dev, int ioaddr);
static int smctr_process_rx_packet(MAC_HEADER *rmf, __u16 size,
        struct net_device *dev, __u16 rx_status);

/* R */
static int smctr_ram_memory_test(struct net_device *dev);
static int smctr_rcv_chg_param(struct net_device *dev, MAC_HEADER *rmf,
        __u16 *correlator);
static int smctr_rcv_init(struct net_device *dev, MAC_HEADER *rmf,
        __u16 *correlator);
static int smctr_rcv_tx_forward(struct net_device *dev, MAC_HEADER *rmf);
static int smctr_rcv_rq_addr_state_attch(struct net_device *dev,
        MAC_HEADER *rmf, __u16 *correlator);
static int smctr_rcv_unknown(struct net_device *dev, MAC_HEADER *rmf,
        __u16 *correlator);
static int smctr_reset_adapter(struct net_device *dev);
static int smctr_restart_tx_chain(struct net_device *dev, short queue);
static int smctr_ring_status_chg(struct net_device *dev);
static int smctr_rx_frame(struct net_device *dev);

/* S */
static int smctr_send_dat(struct net_device *dev);
static netdev_tx_t smctr_send_packet(struct sk_buff *skb,
					   struct net_device *dev);
static int smctr_send_lobe_media_test(struct net_device *dev);
static int smctr_send_rpt_addr(struct net_device *dev, MAC_HEADER *rmf,
        __u16 correlator);
static int smctr_send_rpt_attch(struct net_device *dev, MAC_HEADER *rmf,
        __u16 correlator);
static int smctr_send_rpt_state(struct net_device *dev, MAC_HEADER *rmf,
        __u16 correlator);
static int smctr_send_rpt_tx_forward(struct net_device *dev,
        MAC_HEADER *rmf, __u16 tx_fstatus);
static int smctr_send_rsp(struct net_device *dev, MAC_HEADER *rmf,
        __u16 rcode, __u16 correlator);
static int smctr_send_rq_init(struct net_device *dev);
static int smctr_send_tx_forward(struct net_device *dev, MAC_HEADER *rmf,
        __u16 *tx_fstatus);
static int smctr_set_auth_access_pri(struct net_device *dev,
        MAC_SUB_VECTOR *rsv);
static int smctr_set_auth_funct_class(struct net_device *dev,
        MAC_SUB_VECTOR *rsv);
static int smctr_set_corr(struct net_device *dev, MAC_SUB_VECTOR *rsv,
	__u16 *correlator);
static int smctr_set_error_timer_value(struct net_device *dev,
        MAC_SUB_VECTOR *rsv);
static int smctr_set_frame_forward(struct net_device *dev,
        MAC_SUB_VECTOR *rsv, __u8 dc_sc);
static int smctr_set_local_ring_num(struct net_device *dev,
        MAC_SUB_VECTOR *rsv);
static unsigned short smctr_set_ctrl_attention(struct net_device *dev);
static void smctr_set_multicast_list(struct net_device *dev);
static int smctr_set_page(struct net_device *dev, __u8 *buf);
static int smctr_set_phy_drop(struct net_device *dev,
        MAC_SUB_VECTOR *rsv);
static int smctr_set_ring_speed(struct net_device *dev);
static int smctr_set_rx_look_ahead(struct net_device *dev);
static int smctr_set_trc_reset(int ioaddr);
static int smctr_setup_single_cmd(struct net_device *dev,
        __u16 command, __u16 subcommand);
static int smctr_setup_single_cmd_w_data(struct net_device *dev,
        __u16 command, __u16 subcommand);
static char *smctr_malloc(struct net_device *dev, __u16 size);
static int smctr_status_chg(struct net_device *dev);

/* T */
static void smctr_timeout(struct net_device *dev);
static int smctr_trc_send_packet(struct net_device *dev, FCBlock *fcb,
        __u16 queue);
static __u16 smctr_tx_complete(struct net_device *dev, __u16 queue);
static unsigned short smctr_tx_move_frame(struct net_device *dev,
        struct sk_buff *skb, __u8 *pbuff, unsigned int bytes);

/* U */
static int smctr_update_err_stats(struct net_device *dev);
static int smctr_update_rx_chain(struct net_device *dev, __u16 queue);
static int smctr_update_tx_chain(struct net_device *dev, FCBlock *fcb,
        __u16 queue);

/* W */
static int smctr_wait_cmd(struct net_device *dev);
static int smctr_wait_while_cbusy(struct net_device *dev);

#define TO_256_BYTE_BOUNDRY(X)  (((X + 0xff) & 0xff00) - X)
#define TO_PARAGRAPH_BOUNDRY(X) (((X + 0x0f) & 0xfff0) - X)
#define PARAGRAPH_BOUNDRY(X)    smctr_malloc(dev, TO_PARAGRAPH_BOUNDRY(X))

/* Allocate Adapter Shared Memory.
 * IMPORTANT NOTE: Any changes to this function MUST be mirrored in the
 * function "get_num_rx_bdbs" below!!!
 *
 * Order of memory allocation:
 *
 *       0. Initial System Configuration Block Pointer
 *       1. System Configuration Block
 *       2. System Control Block
 *       3. Action Command Block
 *       4. Interrupt Status Block
 *
 *       5. MAC TX FCB'S
 *       6. NON-MAC TX FCB'S
 *       7. MAC TX BDB'S
 *       8. NON-MAC TX BDB'S
 *       9. MAC RX FCB'S
 *      10. NON-MAC RX FCB'S
 *      11. MAC RX BDB'S
 *      12. NON-MAC RX BDB'S
 *      13. MAC TX Data Buffer( 1, 256 byte buffer)
 *      14. MAC RX Data Buffer( 1, 256 byte buffer)
 *
 *      15. NON-MAC TX Data Buffer
 *      16. NON-MAC RX Data Buffer
 */
static int smctr_alloc_shared_memory(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_alloc_shared_memory\n", dev->name);

        /* Allocate initial System Control Block pointer.
         * This pointer is located in the last page, last offset - 4.
         */
        tp->iscpb_ptr = (ISCPBlock *)(tp->ram_access + ((__u32)64 * 0x400)
                - (long)ISCP_BLOCK_SIZE);

        /* Allocate System Control Blocks. */
        tp->scgb_ptr = (SCGBlock *)smctr_malloc(dev, sizeof(SCGBlock));
        PARAGRAPH_BOUNDRY(tp->sh_mem_used);

        tp->sclb_ptr = (SCLBlock *)smctr_malloc(dev, sizeof(SCLBlock));
        PARAGRAPH_BOUNDRY(tp->sh_mem_used);

        tp->acb_head = (ACBlock *)smctr_malloc(dev,
                sizeof(ACBlock)*tp->num_acbs);
        PARAGRAPH_BOUNDRY(tp->sh_mem_used);

        tp->isb_ptr = (ISBlock *)smctr_malloc(dev, sizeof(ISBlock));
        PARAGRAPH_BOUNDRY(tp->sh_mem_used);

        tp->misc_command_data = (__u16 *)smctr_malloc(dev, MISC_DATA_SIZE);
        PARAGRAPH_BOUNDRY(tp->sh_mem_used);

        /* Allocate transmit FCBs. */
        tp->tx_fcb_head[MAC_QUEUE] = (FCBlock *)smctr_malloc(dev,
                sizeof(FCBlock) * tp->num_tx_fcbs[MAC_QUEUE]);

        tp->tx_fcb_head[NON_MAC_QUEUE] = (FCBlock *)smctr_malloc(dev,
                sizeof(FCBlock) * tp->num_tx_fcbs[NON_MAC_QUEUE]);

        tp->tx_fcb_head[BUG_QUEUE] = (FCBlock *)smctr_malloc(dev,
                sizeof(FCBlock) * tp->num_tx_fcbs[BUG_QUEUE]);

        /* Allocate transmit BDBs. */
        tp->tx_bdb_head[MAC_QUEUE] = (BDBlock *)smctr_malloc(dev,
                sizeof(BDBlock) * tp->num_tx_bdbs[MAC_QUEUE]);

        tp->tx_bdb_head[NON_MAC_QUEUE] = (BDBlock *)smctr_malloc(dev,
                sizeof(BDBlock) * tp->num_tx_bdbs[NON_MAC_QUEUE]);

        tp->tx_bdb_head[BUG_QUEUE] = (BDBlock *)smctr_malloc(dev,
                sizeof(BDBlock) * tp->num_tx_bdbs[BUG_QUEUE]);

        /* Allocate receive FCBs. */
        tp->rx_fcb_head[MAC_QUEUE] = (FCBlock *)smctr_malloc(dev,
                sizeof(FCBlock) * tp->num_rx_fcbs[MAC_QUEUE]);

        tp->rx_fcb_head[NON_MAC_QUEUE] = (FCBlock *)smctr_malloc(dev,
                sizeof(FCBlock) * tp->num_rx_fcbs[NON_MAC_QUEUE]);

        /* Allocate receive BDBs. */
        tp->rx_bdb_head[MAC_QUEUE] = (BDBlock *)smctr_malloc(dev,
                sizeof(BDBlock) * tp->num_rx_bdbs[MAC_QUEUE]);

        tp->rx_bdb_end[MAC_QUEUE] = (BDBlock *)smctr_malloc(dev, 0);

        tp->rx_bdb_head[NON_MAC_QUEUE] = (BDBlock *)smctr_malloc(dev,
                sizeof(BDBlock) * tp->num_rx_bdbs[NON_MAC_QUEUE]);

        tp->rx_bdb_end[NON_MAC_QUEUE] = (BDBlock *)smctr_malloc(dev, 0);

        /* Allocate MAC transmit buffers.
         * MAC Tx Buffers doen't have to be on an ODD Boundry.
         */
        tp->tx_buff_head[MAC_QUEUE]
                = (__u16 *)smctr_malloc(dev, tp->tx_buff_size[MAC_QUEUE]);
        tp->tx_buff_curr[MAC_QUEUE] = tp->tx_buff_head[MAC_QUEUE];
        tp->tx_buff_end [MAC_QUEUE] = (__u16 *)smctr_malloc(dev, 0);

        /* Allocate BUG transmit buffers. */
        tp->tx_buff_head[BUG_QUEUE]
                = (__u16 *)smctr_malloc(dev, tp->tx_buff_size[BUG_QUEUE]);
        tp->tx_buff_curr[BUG_QUEUE] = tp->tx_buff_head[BUG_QUEUE];
        tp->tx_buff_end[BUG_QUEUE] = (__u16 *)smctr_malloc(dev, 0);

        /* Allocate MAC receive data buffers.
         * MAC Rx buffer doesn't have to be on a 256 byte boundary.
         */
        tp->rx_buff_head[MAC_QUEUE] = (__u16 *)smctr_malloc(dev,
                RX_DATA_BUFFER_SIZE * tp->num_rx_bdbs[MAC_QUEUE]);
        tp->rx_buff_end[MAC_QUEUE] = (__u16 *)smctr_malloc(dev, 0);

        /* Allocate Non-MAC transmit buffers.
         * ?? For maximum Netware performance, put Tx Buffers on
         * ODD Boundry and then restore malloc to Even Boundrys.
         */
        smctr_malloc(dev, 1L);
        tp->tx_buff_head[NON_MAC_QUEUE]
                = (__u16 *)smctr_malloc(dev, tp->tx_buff_size[NON_MAC_QUEUE]);
        tp->tx_buff_curr[NON_MAC_QUEUE] = tp->tx_buff_head[NON_MAC_QUEUE];
        tp->tx_buff_end [NON_MAC_QUEUE] = (__u16 *)smctr_malloc(dev, 0);
        smctr_malloc(dev, 1L);

        /* Allocate Non-MAC receive data buffers.
         * To guarantee a minimum of 256 contiguous memory to
         * UM_Receive_Packet's lookahead pointer, before a page
         * change or ring end is encountered, place each rx buffer on
         * a 256 byte boundary.
         */
        smctr_malloc(dev, TO_256_BYTE_BOUNDRY(tp->sh_mem_used));
        tp->rx_buff_head[NON_MAC_QUEUE] = (__u16 *)smctr_malloc(dev,
                RX_DATA_BUFFER_SIZE * tp->num_rx_bdbs[NON_MAC_QUEUE]);
        tp->rx_buff_end[NON_MAC_QUEUE] = (__u16 *)smctr_malloc(dev, 0);

        return (0);
}

/* Enter Bypass state. */
static int smctr_bypass_state(struct net_device *dev)
{
        int err;

	if(smctr_debug > 10)
        	printk(KERN_DEBUG "%s: smctr_bypass_state\n", dev->name);

        err = smctr_setup_single_cmd(dev, ACB_CMD_CHANGE_JOIN_STATE, JS_BYPASS_STATE);

        return (err);
}

static int smctr_checksum_firmware(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        __u16 i, checksum = 0;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_checksum_firmware\n", dev->name);

        smctr_enable_adapter_ctrl_store(dev);

        for(i = 0; i < CS_RAM_SIZE; i += 2)
                checksum += *((__u16 *)(tp->ram_access + i));

        tp->microcode_version = *(__u16 *)(tp->ram_access
                + CS_RAM_VERSION_OFFSET);
        tp->microcode_version >>= 8;

        smctr_disable_adapter_ctrl_store(dev);

        if(checksum)
                return (checksum);

        return (0);
}

static int __init smctr_chk_mca(struct net_device *dev)
{
#ifdef CONFIG_MCA_LEGACY
	struct net_local *tp = netdev_priv(dev);
	int current_slot;
	__u8 r1, r2, r3, r4, r5;

	current_slot = mca_find_unused_adapter(smctr_posid, 0);
	if(current_slot == MCA_NOTFOUND)
		return (-ENODEV);

	mca_set_adapter_name(current_slot, smctr_name);
	mca_mark_as_used(current_slot);
	tp->slot_num = current_slot;

	r1 = mca_read_stored_pos(tp->slot_num, 2);
	r2 = mca_read_stored_pos(tp->slot_num, 3);

	if(tp->slot_num)
		outb(CNFG_POS_CONTROL_REG, (__u8)((tp->slot_num - 1) | CNFG_SLOT_ENABLE_BIT));
	else
		outb(CNFG_POS_CONTROL_REG, (__u8)((tp->slot_num) | CNFG_SLOT_ENABLE_BIT));

	r1 = inb(CNFG_POS_REG1);
	r2 = inb(CNFG_POS_REG0);

	tp->bic_type = BIC_594_CHIP;

	/* IO */
	r2 = mca_read_stored_pos(tp->slot_num, 2);
	r2 &= 0xF0;
	dev->base_addr = ((__u16)r2 << 8) + (__u16)0x800;
	request_region(dev->base_addr, SMCTR_IO_EXTENT, smctr_name);

	/* IRQ */
	r5 = mca_read_stored_pos(tp->slot_num, 5);
	r5 &= 0xC;
        switch(r5)
	{
            	case 0:
			dev->irq = 3;
               		break;

            	case 0x4:
			dev->irq = 4;
               		break;

            	case 0x8:
			dev->irq = 10;
               		break;

            	default:
			dev->irq = 15;
               		break;
	}
	if (request_irq(dev->irq, smctr_interrupt, IRQF_SHARED, smctr_name, dev)) {
		release_region(dev->base_addr, SMCTR_IO_EXTENT);
		return -ENODEV;
	}

	/* Get RAM base */
	r3 = mca_read_stored_pos(tp->slot_num, 3);
	tp->ram_base = ((__u32)(r3 & 0x7) << 13) + 0x0C0000;
	if (r3 & 0x8)
		tp->ram_base += 0x010000;
	if (r3 & 0x80)
		tp->ram_base += 0xF00000;

	/* Get Ram Size */
	r3 &= 0x30;
	r3 >>= 4;

	tp->ram_usable = (__u16)CNFG_SIZE_8KB << r3;
	tp->ram_size = (__u16)CNFG_SIZE_64KB;
	tp->board_id |= TOKEN_MEDIA;

	r4 = mca_read_stored_pos(tp->slot_num, 4);
	tp->rom_base = ((__u32)(r4 & 0x7) << 13) + 0x0C0000;
	if (r4 & 0x8)
		tp->rom_base += 0x010000;

	/* Get ROM size. */
	r4 >>= 4;
	switch (r4) {
		case 0:
			tp->rom_size = CNFG_SIZE_8KB;
			break;
		case 1:
			tp->rom_size = CNFG_SIZE_16KB;
			break;
		case 2:
			tp->rom_size = CNFG_SIZE_32KB;
			break;
		default:
			tp->rom_size = ROM_DISABLE;
	}

	/* Get Media Type. */
	r5 = mca_read_stored_pos(tp->slot_num, 5);
	r5 &= CNFG_MEDIA_TYPE_MASK;
	switch(r5)
	{
		case (0):
			tp->media_type = MEDIA_STP_4;
			break;

		case (1):
			tp->media_type = MEDIA_STP_16;
			break;

		case (3):
			tp->media_type = MEDIA_UTP_16;
			break;

		default:
			tp->media_type = MEDIA_UTP_4;
			break;
	}
	tp->media_menu = 14;

	r2 = mca_read_stored_pos(tp->slot_num, 2);
	if(!(r2 & 0x02))
		tp->mode_bits |= EARLY_TOKEN_REL;

	/* Disable slot */
	outb(CNFG_POS_CONTROL_REG, 0);

	tp->board_id = smctr_get_boardid(dev, 1);
	switch(tp->board_id & 0xffff)
        {
                case WD8115TA:
                        smctr_model = "8115T/A";
                        break;

                case WD8115T:
			if(tp->extra_info & CHIP_REV_MASK)
                                smctr_model = "8115T rev XE";
                        else
                                smctr_model = "8115T rev XD";
                        break;

                default:
                        smctr_model = "Unknown";
                        break;
        }

	return (0);
#else
	return (-1);
#endif /* CONFIG_MCA_LEGACY */
}

static int smctr_chg_rx_mask(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int err = 0;

        if(smctr_debug > 10)
		printk(KERN_DEBUG "%s: smctr_chg_rx_mask\n", dev->name);

        smctr_enable_16bit(dev);
        smctr_set_page(dev, (__u8 *)tp->ram_access);

        if(tp->mode_bits & LOOPING_MODE_MASK)
                tp->config_word0 |= RX_OWN_BIT;
        else
                tp->config_word0 &= ~RX_OWN_BIT;

        if(tp->receive_mask & PROMISCUOUS_MODE)
                tp->config_word0 |= PROMISCUOUS_BIT;
        else
                tp->config_word0 &= ~PROMISCUOUS_BIT;

        if(tp->receive_mask & ACCEPT_ERR_PACKETS)
                tp->config_word0 |= SAVBAD_BIT;
        else
                tp->config_word0 &= ~SAVBAD_BIT;

        if(tp->receive_mask & ACCEPT_ATT_MAC_FRAMES)
                tp->config_word0 |= RXATMAC;
        else
                tp->config_word0 &= ~RXATMAC;

        if(tp->receive_mask & ACCEPT_MULTI_PROM)
                tp->config_word1 |= MULTICAST_ADDRESS_BIT;
        else
                tp->config_word1 &= ~MULTICAST_ADDRESS_BIT;

        if(tp->receive_mask & ACCEPT_SOURCE_ROUTING_SPANNING)
                tp->config_word1 |= SOURCE_ROUTING_SPANNING_BITS;
        else
        {
                if(tp->receive_mask & ACCEPT_SOURCE_ROUTING)
                        tp->config_word1 |= SOURCE_ROUTING_EXPLORER_BIT;
                else
                        tp->config_word1 &= ~SOURCE_ROUTING_SPANNING_BITS;
        }

        if((err = smctr_issue_write_word_cmd(dev, RW_CONFIG_REGISTER_0,
                &tp->config_word0)))
        {
                return (err);
        }

        if((err = smctr_issue_write_word_cmd(dev, RW_CONFIG_REGISTER_1,
                &tp->config_word1)))
        {
                return (err);
        }

        smctr_disable_16bit(dev);

        return (0);
}

static int smctr_clear_int(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);

        outb((tp->trc_mask | CSR_CLRTINT), dev->base_addr + CSR);

        return (0);
}

static int smctr_clear_trc_reset(int ioaddr)
{
        __u8 r;

        r = inb(ioaddr + MSR);
        outb(~MSR_RST & r, ioaddr + MSR);

        return (0);
}

/*
 * The inverse routine to smctr_open().
 */
static int smctr_close(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        struct sk_buff *skb;
        int err;

	netif_stop_queue(dev);
	
	tp->cleanup = 1;

        /* Check to see if adapter is already in a closed state. */
        if(tp->status != OPEN)
                return (0);

        smctr_enable_16bit(dev);
        smctr_set_page(dev, (__u8 *)tp->ram_access);

        if((err = smctr_issue_remove_cmd(dev)))
        {
                smctr_disable_16bit(dev);
                return (err);
        }

        for(;;)
        {
                skb = skb_dequeue(&tp->SendSkbQueue);
                if(skb == NULL)
                        break;
                tp->QueueSkb++;
                dev_kfree_skb(skb);
        }


        return (0);
}

static int smctr_decode_firmware(struct net_device *dev,
				 const struct firmware *fw)
{
        struct net_local *tp = netdev_priv(dev);
        short bit = 0x80, shift = 12;
        DECODE_TREE_NODE *tree;
        short branch, tsize;
        __u16 buff = 0;
        long weight;
        __u8 *ucode;
        __u16 *mem;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_decode_firmware\n", dev->name);

        weight  = *(long *)(fw->data + WEIGHT_OFFSET);
        tsize   = *(__u8 *)(fw->data + TREE_SIZE_OFFSET);
        tree    = (DECODE_TREE_NODE *)(fw->data + TREE_OFFSET);
        ucode   = (__u8 *)(fw->data + TREE_OFFSET
                        + (tsize * sizeof(DECODE_TREE_NODE)));
        mem     = (__u16 *)(tp->ram_access);

        while(weight)
        {
                branch = ROOT;
                while((tree + branch)->tag != LEAF && weight)
                {
                        branch = *ucode & bit ? (tree + branch)->llink
                                : (tree + branch)->rlink;

                        bit >>= 1;
                        weight--;

                        if(bit == 0)
                        {
                                bit = 0x80;
                                ucode++;
                        }
                }

                buff |= (tree + branch)->info << shift;
                shift -= 4;

                if(shift < 0)
                {
                        *(mem++) = SWAP_BYTES(buff);
                        buff    = 0;
                        shift   = 12;
                }
        }

        /* The following assumes the Control Store Memory has
         * been initialized to zero. If the last partial word
         * is zero, it will not be written.
         */
        if(buff)
                *(mem++) = SWAP_BYTES(buff);

        return (0);
}

static int smctr_disable_16bit(struct net_device *dev)
{
        return (0);
}

/*
 * On Exit, Adapter is:
 * 1. TRC is in a reset state and un-initialized.
 * 2. Adapter memory is enabled.
 * 3. Control Store memory is out of context (-WCSS is 1).
 */
static int smctr_disable_adapter_ctrl_store(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int ioaddr = dev->base_addr;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_disable_adapter_ctrl_store\n", dev->name);

        tp->trc_mask |= CSR_WCSS;
        outb(tp->trc_mask, ioaddr + CSR);

        return (0);
}

static int smctr_disable_bic_int(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int ioaddr = dev->base_addr;

        tp->trc_mask = CSR_MSK_ALL | CSR_MSKCBUSY
	        | CSR_MSKTINT | CSR_WCSS;
        outb(tp->trc_mask, ioaddr + CSR);

        return (0);
}

static int smctr_enable_16bit(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        __u8    r;

        if(tp->adapter_bus == BUS_ISA16_TYPE)
        {
                r = inb(dev->base_addr + LAAR);
                outb((r | LAAR_MEM16ENB), dev->base_addr + LAAR);
        }

        return (0);
}

/*
 * To enable the adapter control store memory:
 * 1. Adapter must be in a RESET state.
 * 2. Adapter memory must be enabled.
 * 3. Control Store Memory is in context (-WCSS is 0).
 */
static int smctr_enable_adapter_ctrl_store(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int ioaddr = dev->base_addr;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_enable_adapter_ctrl_store\n", dev->name);

        smctr_set_trc_reset(ioaddr);
        smctr_enable_adapter_ram(dev);

        tp->trc_mask &= ~CSR_WCSS;
        outb(tp->trc_mask, ioaddr + CSR);

        return (0);
}

static int smctr_enable_adapter_ram(struct net_device *dev)
{
        int ioaddr = dev->base_addr;
        __u8 r;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_enable_adapter_ram\n", dev->name);

        r = inb(ioaddr + MSR);
        outb(MSR_MEMB | r, ioaddr + MSR);

        return (0);
}

static int smctr_enable_bic_int(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int ioaddr = dev->base_addr;
        __u8 r;

        switch(tp->bic_type)
        {
                case (BIC_584_CHIP):
                        tp->trc_mask = CSR_MSKCBUSY | CSR_WCSS;
                        outb(tp->trc_mask, ioaddr + CSR);
                        r = inb(ioaddr + IRR);
                        outb(r | IRR_IEN, ioaddr + IRR);
                        break;

                case (BIC_594_CHIP):
                        tp->trc_mask = CSR_MSKCBUSY | CSR_WCSS;
                        outb(tp->trc_mask, ioaddr + CSR);
                        r = inb(ioaddr + IMCCR);
                        outb(r | IMCCR_EIL, ioaddr + IMCCR);
                        break;
        }

        return (0);
}

static int __init smctr_chk_isa(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int ioaddr = dev->base_addr;
        __u8 r1, r2, b, chksum = 0;
        __u16 r;
	int i;
	int err = -ENODEV;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_chk_isa %#4x\n", dev->name, ioaddr);

	if((ioaddr & 0x1F) != 0)
                goto out;

        /* Grab the region so that no one else tries to probe our ioports. */
	if (!request_region(ioaddr, SMCTR_IO_EXTENT, smctr_name)) {
		err = -EBUSY;
		goto out;
	}

        /* Checksum SMC node address */
        for(i = 0; i < 8; i++)
        {
                b = inb(ioaddr + LAR0 + i);
                chksum += b;
        }

        if (chksum != NODE_ADDR_CKSUM)
                goto out2;

        b = inb(ioaddr + BDID);
	if(b != BRD_ID_8115T)
        {
                printk(KERN_ERR "%s: The adapter found is not supported\n", dev->name);
                goto out2;
        }

        /* Check for 8115T Board ID */
        r2 = 0;
        for(r = 0; r < 8; r++)
        {
            r1 = inb(ioaddr + 0x8 + r);
            r2 += r1;
        }

        /* value of RegF adds up the sum to 0xFF */
        if((r2 != 0xFF) && (r2 != 0xEE))
                goto out2;

        /* Get adapter ID */
        tp->board_id = smctr_get_boardid(dev, 0);
        switch(tp->board_id & 0xffff)
        {
                case WD8115TA:
                        smctr_model = "8115T/A";
                        break;

                case WD8115T:
			if(tp->extra_info & CHIP_REV_MASK)
                                smctr_model = "8115T rev XE";
                        else
                                smctr_model = "8115T rev XD";
                        break;

                default:
                        smctr_model = "Unknown";
                        break;
        }

        /* Store BIC type. */
        tp->bic_type = BIC_584_CHIP;
        tp->nic_type = NIC_825_CHIP;

        /* Copy Ram Size */
        tp->ram_usable  = CNFG_SIZE_16KB;
        tp->ram_size    = CNFG_SIZE_64KB;

        /* Get 58x Ram Base */
        r1 = inb(ioaddr);
        r1 &= 0x3F;

        r2 = inb(ioaddr + CNFG_LAAR_584);
        r2 &= CNFG_LAAR_MASK;
        r2 <<= 3;
        r2 |= ((r1 & 0x38) >> 3);

        tp->ram_base = ((__u32)r2 << 16) + (((__u32)(r1 & 0x7)) << 13);

        /* Get 584 Irq */
        r1 = 0;
        r1 = inb(ioaddr + CNFG_ICR_583);
        r1 &= CNFG_ICR_IR2_584;

        r2 = inb(ioaddr + CNFG_IRR_583);
        r2 &= CNFG_IRR_IRQS;     /* 0x60 */
        r2 >>= 5;

        switch(r2)
        {
                case 0:
                        if(r1 == 0)
                                dev->irq = 2;
                        else
                                dev->irq = 10;
                        break;

                case 1:
                        if(r1 == 0)
                                dev->irq = 3;
                        else
                                dev->irq = 11;
                        break;

                case 2:
                        if(r1 == 0)
                        {
                                if(tp->extra_info & ALTERNATE_IRQ_BIT)
                                        dev->irq = 5;
                                else
                                        dev->irq = 4;
                        }
                        else
                                dev->irq = 15;
                        break;

                case 3:
                        if(r1 == 0)
                                dev->irq = 7;
                        else
                                dev->irq = 4;
                        break;

                default:
                        printk(KERN_ERR "%s: No IRQ found aborting\n", dev->name);
                        goto out2;
         }

        if (request_irq(dev->irq, smctr_interrupt, IRQF_SHARED, smctr_name, dev))
                goto out2;

        /* Get 58x Rom Base */
        r1 = inb(ioaddr + CNFG_BIO_583);
        r1 &= 0x3E;
        r1 |= 0x40;

        tp->rom_base = (__u32)r1 << 13;

        /* Get 58x Rom Size */
        r1 = inb(ioaddr + CNFG_BIO_583);
        r1 &= 0xC0;
        if(r1 == 0)
                tp->rom_size = ROM_DISABLE;
        else
        {
                r1 >>= 6;
                tp->rom_size = (__u16)CNFG_SIZE_8KB << r1;
        }

        /* Get 58x Boot Status */
        r1 = inb(ioaddr + CNFG_GP2);

        tp->mode_bits &= (~BOOT_STATUS_MASK);

        if(r1 & CNFG_GP2_BOOT_NIBBLE)
                tp->mode_bits |= BOOT_TYPE_1;

        /* Get 58x Zero Wait State */
        tp->mode_bits &= (~ZERO_WAIT_STATE_MASK);

        r1 = inb(ioaddr + CNFG_IRR_583);

        if(r1 & CNFG_IRR_ZWS)
                 tp->mode_bits |= ZERO_WAIT_STATE_8_BIT;

        if(tp->board_id & BOARD_16BIT)
        {
                r1 = inb(ioaddr + CNFG_LAAR_584);

                if(r1 & CNFG_LAAR_ZWS)
                        tp->mode_bits |= ZERO_WAIT_STATE_16_BIT;
        }

        /* Get 584 Media Menu */
        tp->media_menu = 14;
        r1 = inb(ioaddr + CNFG_IRR_583);

        tp->mode_bits &= 0xf8ff;       /* (~CNFG_INTERFACE_TYPE_MASK) */
        if((tp->board_id & TOKEN_MEDIA) == TOKEN_MEDIA)
        {
                /* Get Advanced Features */
                if(((r1 & 0x6) >> 1) == 0x3)
                        tp->media_type |= MEDIA_UTP_16;
                else
                {
                        if(((r1 & 0x6) >> 1) == 0x2)
                                tp->media_type |= MEDIA_STP_16;
                        else
                        {
                                if(((r1 & 0x6) >> 1) == 0x1)
                                        tp->media_type |= MEDIA_UTP_4;

                                else
                                        tp->media_type |= MEDIA_STP_4;
                        }
                }

                r1 = inb(ioaddr + CNFG_GP2);
                if(!(r1 & 0x2) )           /* GP2_ETRD */
                        tp->mode_bits |= EARLY_TOKEN_REL;

                /* see if the chip is corrupted
                if(smctr_read_584_chksum(ioaddr))
                {
                        printk(KERN_ERR "%s: EEPROM Checksum Failure\n", dev->name);
			free_irq(dev->irq, dev);
                        goto out2;
                }
		*/
        }

        return (0);

out2:
	release_region(ioaddr, SMCTR_IO_EXTENT);
out:
	return err;
}

static int __init smctr_get_boardid(struct net_device *dev, int mca)
{
        struct net_local *tp = netdev_priv(dev);
        int ioaddr = dev->base_addr;
        __u8 r, r1, IdByte;
        __u16 BoardIdMask;

        tp->board_id = BoardIdMask = 0;

	if(mca)
	{
		BoardIdMask |= (MICROCHANNEL+INTERFACE_CHIP+TOKEN_MEDIA+PAGED_RAM+BOARD_16BIT);
		tp->extra_info |= (INTERFACE_594_CHIP+RAM_SIZE_64K+NIC_825_BIT+ALTERNATE_IRQ_BIT+SLOT_16BIT);
	}
	else
	{
        	BoardIdMask|=(INTERFACE_CHIP+TOKEN_MEDIA+PAGED_RAM+BOARD_16BIT);
        	tp->extra_info |= (INTERFACE_584_CHIP + RAM_SIZE_64K
        	        + NIC_825_BIT + ALTERNATE_IRQ_BIT);
	}

	if(!mca)
	{
        	r = inb(ioaddr + BID_REG_1);
        	r &= 0x0c;
       		outb(r, ioaddr + BID_REG_1);
        	r = inb(ioaddr + BID_REG_1);

        	if(r & BID_SIXTEEN_BIT_BIT)
        	{
        	        tp->extra_info |= SLOT_16BIT;
        	        tp->adapter_bus = BUS_ISA16_TYPE;
        	}
        	else
        	        tp->adapter_bus = BUS_ISA8_TYPE;
	}
	else
		tp->adapter_bus = BUS_MCA_TYPE;

        /* Get Board Id Byte */
        IdByte = inb(ioaddr + BID_BOARD_ID_BYTE);

        /* if Major version > 1.0 then
         *      return;
         */
        if(IdByte & 0xF8)
                return (-1);

        r1 = inb(ioaddr + BID_REG_1);
        r1 &= BID_ICR_MASK;
        r1 |= BID_OTHER_BIT;

        outb(r1, ioaddr + BID_REG_1);
        r1 = inb(ioaddr + BID_REG_3);

        r1 &= BID_EAR_MASK;
        r1 |= BID_ENGR_PAGE;

        outb(r1, ioaddr + BID_REG_3);
        r1 = inb(ioaddr + BID_REG_1);
        r1 &= BID_ICR_MASK;
        r1 |= (BID_RLA | BID_OTHER_BIT);

        outb(r1, ioaddr + BID_REG_1);

        r1 = inb(ioaddr + BID_REG_1);
        while(r1 & BID_RECALL_DONE_MASK)
                r1 = inb(ioaddr + BID_REG_1);

        r = inb(ioaddr + BID_LAR_0 + BID_REG_6);

        /* clear chip rev bits */
        tp->extra_info &= ~CHIP_REV_MASK;
        tp->extra_info |= ((r & BID_EEPROM_CHIP_REV_MASK) << 6);

        r1 = inb(ioaddr + BID_REG_1);
        r1 &= BID_ICR_MASK;
        r1 |= BID_OTHER_BIT;

        outb(r1, ioaddr + BID_REG_1);
        r1 = inb(ioaddr + BID_REG_3);

        r1 &= BID_EAR_MASK;
        r1 |= BID_EA6;

        outb(r1, ioaddr + BID_REG_3);
        r1 = inb(ioaddr + BID_REG_1);

        r1 &= BID_ICR_MASK;
        r1 |= BID_RLA;

        outb(r1, ioaddr + BID_REG_1);
        r1 = inb(ioaddr + BID_REG_1);

        while(r1 & BID_RECALL_DONE_MASK)
                r1 = inb(ioaddr + BID_REG_1);

        return (BoardIdMask);
}

static int smctr_get_group_address(struct net_device *dev)
{
        smctr_issue_read_word_cmd(dev, RW_INDIVIDUAL_GROUP_ADDR);

        return(smctr_wait_cmd(dev));
}

static int smctr_get_functional_address(struct net_device *dev)
{
        smctr_issue_read_word_cmd(dev, RW_FUNCTIONAL_ADDR);

        return(smctr_wait_cmd(dev));
}

/* Calculate number of Non-MAC receive BDB's and data buffers.
 * This function must simulate allocateing shared memory exactly
 * as the allocate_shared_memory function above.
 */
static unsigned int smctr_get_num_rx_bdbs(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int mem_used = 0;

        /* Allocate System Control Blocks. */
        mem_used += sizeof(SCGBlock);

        mem_used += TO_PARAGRAPH_BOUNDRY(mem_used);
        mem_used += sizeof(SCLBlock);

        mem_used += TO_PARAGRAPH_BOUNDRY(mem_used);
        mem_used += sizeof(ACBlock) * tp->num_acbs;

        mem_used += TO_PARAGRAPH_BOUNDRY(mem_used);
        mem_used += sizeof(ISBlock);

        mem_used += TO_PARAGRAPH_BOUNDRY(mem_used);
        mem_used += MISC_DATA_SIZE;

        /* Allocate transmit FCB's. */
        mem_used += TO_PARAGRAPH_BOUNDRY(mem_used);

        mem_used += sizeof(FCBlock) * tp->num_tx_fcbs[MAC_QUEUE];
        mem_used += sizeof(FCBlock) * tp->num_tx_fcbs[NON_MAC_QUEUE];
        mem_used += sizeof(FCBlock) * tp->num_tx_fcbs[BUG_QUEUE];

        /* Allocate transmit BDBs. */
        mem_used += sizeof(BDBlock) * tp->num_tx_bdbs[MAC_QUEUE];
        mem_used += sizeof(BDBlock) * tp->num_tx_bdbs[NON_MAC_QUEUE];
        mem_used += sizeof(BDBlock) * tp->num_tx_bdbs[BUG_QUEUE];

        /* Allocate receive FCBs. */
        mem_used += sizeof(FCBlock) * tp->num_rx_fcbs[MAC_QUEUE];
        mem_used += sizeof(FCBlock) * tp->num_rx_fcbs[NON_MAC_QUEUE];

        /* Allocate receive BDBs. */
        mem_used += sizeof(BDBlock) * tp->num_rx_bdbs[MAC_QUEUE];

        /* Allocate MAC transmit buffers.
         * MAC transmit buffers don't have to be on an ODD Boundry.
         */
        mem_used += tp->tx_buff_size[MAC_QUEUE];

        /* Allocate BUG transmit buffers. */
        mem_used += tp->tx_buff_size[BUG_QUEUE];

        /* Allocate MAC receive data buffers.
         * MAC receive buffers don't have to be on a 256 byte boundary.
         */
        mem_used += RX_DATA_BUFFER_SIZE * tp->num_rx_bdbs[MAC_QUEUE];

        /* Allocate Non-MAC transmit buffers.
         * For maximum Netware performance, put Tx Buffers on
         * ODD Boundry,and then restore malloc to Even Boundrys.
         */
        mem_used += 1L;
        mem_used += tp->tx_buff_size[NON_MAC_QUEUE];
        mem_used += 1L;

        /* CALCULATE NUMBER OF NON-MAC RX BDB'S
         * AND NON-MAC RX DATA BUFFERS
         *
         * Make sure the mem_used offset at this point is the
         * same as in allocate_shared memory or the following
         * boundary adjustment will be incorrect (i.e. not allocating
         * the non-mac receive buffers above cannot change the 256
         * byte offset).
         *
         * Since this cannot be guaranteed, adding the full 256 bytes
         * to the amount of shared memory used at this point will guaranteed
         * that the rx data buffers do not overflow shared memory.
         */
        mem_used += 0x100;

        return((0xffff - mem_used) / (RX_DATA_BUFFER_SIZE + sizeof(BDBlock)));
}

static int smctr_get_physical_drop_number(struct net_device *dev)
{
        smctr_issue_read_word_cmd(dev, RW_PHYSICAL_DROP_NUMBER);

        return(smctr_wait_cmd(dev));
}

static __u8 * smctr_get_rx_pointer(struct net_device *dev, short queue)
{
        struct net_local *tp = netdev_priv(dev);
        BDBlock *bdb;

        bdb = (BDBlock *)((__u32)tp->ram_access
                + (__u32)(tp->rx_fcb_curr[queue]->trc_bdb_ptr));

        tp->rx_fcb_curr[queue]->bdb_ptr = bdb;

        return ((__u8 *)bdb->data_block_ptr);
}

static int smctr_get_station_id(struct net_device *dev)
{
        smctr_issue_read_word_cmd(dev, RW_INDIVIDUAL_MAC_ADDRESS);

        return(smctr_wait_cmd(dev));
}

/*
 * Get the current statistics. This may be called with the card open
 * or closed.
 */
static struct net_device_stats *smctr_get_stats(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);

        return ((struct net_device_stats *)&tp->MacStat);
}

static FCBlock *smctr_get_tx_fcb(struct net_device *dev, __u16 queue,
        __u16 bytes_count)
{
        struct net_local *tp = netdev_priv(dev);
        FCBlock *pFCB;
        BDBlock *pbdb;
        unsigned short alloc_size;
        unsigned short *temp;

        if(smctr_debug > 20)
                printk(KERN_DEBUG "smctr_get_tx_fcb\n");

        /* check if there is enough FCB blocks */
        if(tp->num_tx_fcbs_used[queue] >= tp->num_tx_fcbs[queue])
                return ((FCBlock *)(-1L));

        /* round off the input pkt size to the nearest even number */
        alloc_size = (bytes_count + 1) & 0xfffe;

        /* check if enough mem */
        if((tp->tx_buff_used[queue] + alloc_size) > tp->tx_buff_size[queue])
                return ((FCBlock *)(-1L));

        /* check if past the end ;
         * if exactly enough mem to end of ring, alloc from front.
         * this avoids update of curr when curr = end
         */
        if(((unsigned long)(tp->tx_buff_curr[queue]) + alloc_size)
                >= (unsigned long)(tp->tx_buff_end[queue]))
        {
                /* check if enough memory from ring head */
                alloc_size = alloc_size +
                        (__u16)((__u32)tp->tx_buff_end[queue]
                        - (__u32)tp->tx_buff_curr[queue]);

                if((tp->tx_buff_used[queue] + alloc_size)
                        > tp->tx_buff_size[queue])
                {
                        return ((FCBlock *)(-1L));
                }

                /* ring wrap */
                tp->tx_buff_curr[queue] = tp->tx_buff_head[queue];
        }

        tp->tx_buff_used[queue] += alloc_size;
        tp->num_tx_fcbs_used[queue]++;
        tp->tx_fcb_curr[queue]->frame_length = bytes_count;
        tp->tx_fcb_curr[queue]->memory_alloc = alloc_size;
        temp = tp->tx_buff_curr[queue];
        tp->tx_buff_curr[queue]
                = (__u16 *)((__u32)temp + (__u32)((bytes_count + 1) & 0xfffe));

        pbdb = tp->tx_fcb_curr[queue]->bdb_ptr;
        pbdb->buffer_length = bytes_count;
        pbdb->data_block_ptr = temp;
        pbdb->trc_data_block_ptr = TRC_POINTER(temp);

        pFCB = tp->tx_fcb_curr[queue];
        tp->tx_fcb_curr[queue] = tp->tx_fcb_curr[queue]->next_ptr;

        return (pFCB);
}

static int smctr_get_upstream_neighbor_addr(struct net_device *dev)
{
        smctr_issue_read_word_cmd(dev, RW_UPSTREAM_NEIGHBOR_ADDRESS);

        return(smctr_wait_cmd(dev));
}

static int smctr_hardware_send_packet(struct net_device *dev,
        struct net_local *tp)
{
        struct tr_statistics *tstat = &tp->MacStat;
        struct sk_buff *skb;
        FCBlock *fcb;

        if(smctr_debug > 10)
                printk(KERN_DEBUG"%s: smctr_hardware_send_packet\n", dev->name);

        if(tp->status != OPEN)
                return (-1);

        if(tp->monitor_state_ready != 1)
                return (-1);

        for(;;)
        {
                /* Send first buffer from queue */
                skb = skb_dequeue(&tp->SendSkbQueue);
                if(skb == NULL)
                        return (-1);

                tp->QueueSkb++;

                if(skb->len < SMC_HEADER_SIZE || skb->len > tp->max_packet_size)                        return (-1);

                smctr_enable_16bit(dev);
                smctr_set_page(dev, (__u8 *)tp->ram_access);

                if((fcb = smctr_get_tx_fcb(dev, NON_MAC_QUEUE, skb->len))
                        == (FCBlock *)(-1L))
                {
                        smctr_disable_16bit(dev);
                        return (-1);
                }

                smctr_tx_move_frame(dev, skb,
                        (__u8 *)fcb->bdb_ptr->data_block_ptr, skb->len);

                smctr_set_page(dev, (__u8 *)fcb);

                smctr_trc_send_packet(dev, fcb, NON_MAC_QUEUE);
                dev_kfree_skb(skb);

                tstat->tx_packets++;

                smctr_disable_16bit(dev);
        }

        return (0);
}

static int smctr_init_acbs(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i;
        ACBlock *acb;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_init_acbs\n", dev->name);

        acb                     = tp->acb_head;
        acb->cmd_done_status    = (ACB_COMMAND_DONE | ACB_COMMAND_SUCCESSFUL);
        acb->cmd_info           = ACB_CHAIN_END;
        acb->cmd                = 0;
        acb->subcmd             = 0;
        acb->data_offset_lo     = 0;
        acb->data_offset_hi     = 0;
        acb->next_ptr
                = (ACBlock *)(((char *)acb) + sizeof(ACBlock));
        acb->trc_next_ptr       = TRC_POINTER(acb->next_ptr);

        for(i = 1; i < tp->num_acbs; i++)
        {
                acb             = acb->next_ptr;
                acb->cmd_done_status
                        = (ACB_COMMAND_DONE | ACB_COMMAND_SUCCESSFUL);
                acb->cmd_info = ACB_CHAIN_END;
                acb->cmd        = 0;
                acb->subcmd     = 0;
                acb->data_offset_lo = 0;
                acb->data_offset_hi = 0;
                acb->next_ptr
                        = (ACBlock *)(((char *)acb) + sizeof(ACBlock));
                acb->trc_next_ptr = TRC_POINTER(acb->next_ptr);
        }

        acb->next_ptr           = tp->acb_head;
        acb->trc_next_ptr       = TRC_POINTER(tp->acb_head);
        tp->acb_next            = tp->acb_head->next_ptr;
        tp->acb_curr            = tp->acb_head->next_ptr;
        tp->num_acbs_used       = 0;

        return (0);
}

static int smctr_init_adapter(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int err;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_init_adapter\n", dev->name);

        tp->status              = CLOSED;
        tp->page_offset_mask    = (tp->ram_usable * 1024) - 1;
        skb_queue_head_init(&tp->SendSkbQueue);
        tp->QueueSkb = MAX_TX_QUEUE;

        if(!(tp->group_address_0 & 0x0080))
                tp->group_address_0 |= 0x00C0;

        if(!(tp->functional_address_0 & 0x00C0))
                tp->functional_address_0 |= 0x00C0;

        tp->functional_address[0] &= 0xFF7F;

        if(tp->authorized_function_classes == 0)
                tp->authorized_function_classes = 0x7FFF;

        if(tp->authorized_access_priority == 0)
                tp->authorized_access_priority = 0x06;

        smctr_disable_bic_int(dev);
        smctr_set_trc_reset(dev->base_addr);

        smctr_enable_16bit(dev);
        smctr_set_page(dev, (__u8 *)tp->ram_access);

        if(smctr_checksum_firmware(dev))
	{
                printk(KERN_ERR "%s: Previously loaded firmware is missing\n",dev->name);                return (-ENOENT);
        }

        if((err = smctr_ram_memory_test(dev)))
	{
                printk(KERN_ERR "%s: RAM memory test failed.\n", dev->name);
                return (-EIO);
        }

	smctr_set_rx_look_ahead(dev);
        smctr_load_node_addr(dev);

        /* Initialize adapter for Internal Self Test. */
        smctr_reset_adapter(dev);
        if((err = smctr_init_card_real(dev)))
	{
                printk(KERN_ERR "%s: Initialization of card failed (%d)\n",
                        dev->name, err);
                return (-EINVAL);
        }

        /* This routine clobbers the TRC's internal registers. */
        if((err = smctr_internal_self_test(dev)))
	{
                printk(KERN_ERR "%s: Card failed internal self test (%d)\n",
                        dev->name, err);
                return (-EINVAL);
        }

        /* Re-Initialize adapter's internal registers */
        smctr_reset_adapter(dev);
        if((err = smctr_init_card_real(dev)))
	{
                printk(KERN_ERR "%s: Initialization of card failed (%d)\n",
                        dev->name, err);
                return (-EINVAL);
        }

        smctr_enable_bic_int(dev);

        if((err = smctr_issue_enable_int_cmd(dev, TRC_INTERRUPT_ENABLE_MASK)))
                return (err);

        smctr_disable_16bit(dev);

        return (0);
}

static int smctr_init_card_real(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int err = 0;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_init_card_real\n", dev->name);

        tp->sh_mem_used = 0;
        tp->num_acbs    = NUM_OF_ACBS;

        /* Range Check Max Packet Size */
        if(tp->max_packet_size < 256)
                tp->max_packet_size = 256;
        else
        {
                if(tp->max_packet_size > NON_MAC_TX_BUFFER_MEMORY)
                        tp->max_packet_size = NON_MAC_TX_BUFFER_MEMORY;
        }

        tp->num_of_tx_buffs = (NON_MAC_TX_BUFFER_MEMORY
                / tp->max_packet_size) - 1;

        if(tp->num_of_tx_buffs > NUM_NON_MAC_TX_FCBS)
                tp->num_of_tx_buffs = NUM_NON_MAC_TX_FCBS;
        else
        {
                if(tp->num_of_tx_buffs == 0)
                        tp->num_of_tx_buffs = 1;
        }

        /* Tx queue constants */
        tp->num_tx_fcbs        [BUG_QUEUE]     = NUM_BUG_TX_FCBS;
        tp->num_tx_bdbs        [BUG_QUEUE]     = NUM_BUG_TX_BDBS;
        tp->tx_buff_size       [BUG_QUEUE]     = BUG_TX_BUFFER_MEMORY;
        tp->tx_buff_used       [BUG_QUEUE]     = 0;
        tp->tx_queue_status    [BUG_QUEUE]     = NOT_TRANSMITING;

        tp->num_tx_fcbs        [MAC_QUEUE]     = NUM_MAC_TX_FCBS;
        tp->num_tx_bdbs        [MAC_QUEUE]     = NUM_MAC_TX_BDBS;
        tp->tx_buff_size       [MAC_QUEUE]     = MAC_TX_BUFFER_MEMORY;
        tp->tx_buff_used       [MAC_QUEUE]     = 0;
        tp->tx_queue_status    [MAC_QUEUE]     = NOT_TRANSMITING;

        tp->num_tx_fcbs        [NON_MAC_QUEUE] = NUM_NON_MAC_TX_FCBS;
        tp->num_tx_bdbs        [NON_MAC_QUEUE] = NUM_NON_MAC_TX_BDBS;
        tp->tx_buff_size       [NON_MAC_QUEUE] = NON_MAC_TX_BUFFER_MEMORY;
        tp->tx_buff_used       [NON_MAC_QUEUE] = 0;
        tp->tx_queue_status    [NON_MAC_QUEUE] = NOT_TRANSMITING;

        /* Receive Queue Constants */
        tp->num_rx_fcbs[MAC_QUEUE] = NUM_MAC_RX_FCBS;
        tp->num_rx_bdbs[MAC_QUEUE] = NUM_MAC_RX_BDBS;

        if(tp->extra_info & CHIP_REV_MASK)
                tp->num_rx_fcbs[NON_MAC_QUEUE] = 78;    /* 825 Rev. XE */
        else
                tp->num_rx_fcbs[NON_MAC_QUEUE] = 7;     /* 825 Rev. XD */

        tp->num_rx_bdbs[NON_MAC_QUEUE] = smctr_get_num_rx_bdbs(dev);

        smctr_alloc_shared_memory(dev);
        smctr_init_shared_memory(dev);

        if((err = smctr_issue_init_timers_cmd(dev)))
                return (err);

        if((err = smctr_issue_init_txrx_cmd(dev)))
	{
                printk(KERN_ERR "%s: Hardware failure\n", dev->name);
                return (err);
        }

        return (0);
}

static int smctr_init_rx_bdbs(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i, j;
        BDBlock *bdb;
        __u16 *buf;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_init_rx_bdbs\n", dev->name);

        for(i = 0; i < NUM_RX_QS_USED; i++)
        {
                bdb = tp->rx_bdb_head[i];
                buf = tp->rx_buff_head[i];
                bdb->info = (BDB_CHAIN_END | BDB_NO_WARNING);
                bdb->buffer_length = RX_DATA_BUFFER_SIZE;
                bdb->next_ptr = (BDBlock *)(((char *)bdb) + sizeof(BDBlock));
                bdb->data_block_ptr = buf;
                bdb->trc_next_ptr = TRC_POINTER(bdb->next_ptr);

                if(i == NON_MAC_QUEUE)
                        bdb->trc_data_block_ptr = RX_BUFF_TRC_POINTER(buf);
                else
                        bdb->trc_data_block_ptr = TRC_POINTER(buf);

                for(j = 1; j < tp->num_rx_bdbs[i]; j++)
                {
                        bdb->next_ptr->back_ptr = bdb;
                        bdb = bdb->next_ptr;
                        buf = (__u16 *)((char *)buf + RX_DATA_BUFFER_SIZE);
                        bdb->info = (BDB_NOT_CHAIN_END | BDB_NO_WARNING);
                        bdb->buffer_length = RX_DATA_BUFFER_SIZE;
                        bdb->next_ptr = (BDBlock *)(((char *)bdb) + sizeof(BDBlock));
                        bdb->data_block_ptr = buf;
                        bdb->trc_next_ptr = TRC_POINTER(bdb->next_ptr);

                        if(i == NON_MAC_QUEUE)
                                bdb->trc_data_block_ptr = RX_BUFF_TRC_POINTER(buf);
                        else
                                bdb->trc_data_block_ptr = TRC_POINTER(buf);
                }

                bdb->next_ptr           = tp->rx_bdb_head[i];
                bdb->trc_next_ptr       = TRC_POINTER(tp->rx_bdb_head[i]);

                tp->rx_bdb_head[i]->back_ptr    = bdb;
                tp->rx_bdb_curr[i]              = tp->rx_bdb_head[i]->next_ptr;
        }

        return (0);
}

static int smctr_init_rx_fcbs(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i, j;
        FCBlock *fcb;

        for(i = 0; i < NUM_RX_QS_USED; i++)
        {
                fcb               = tp->rx_fcb_head[i];
                fcb->frame_status = 0;
                fcb->frame_length = 0;
                fcb->info         = FCB_CHAIN_END;
                fcb->next_ptr     = (FCBlock *)(((char*)fcb) + sizeof(FCBlock));
                if(i == NON_MAC_QUEUE)
                        fcb->trc_next_ptr = RX_FCB_TRC_POINTER(fcb->next_ptr);
                else
                        fcb->trc_next_ptr = TRC_POINTER(fcb->next_ptr);

                for(j = 1; j < tp->num_rx_fcbs[i]; j++)
                {
                        fcb->next_ptr->back_ptr = fcb;
                        fcb                     = fcb->next_ptr;
                        fcb->frame_status       = 0;
                        fcb->frame_length       = 0;
                        fcb->info               = FCB_WARNING;
                        fcb->next_ptr
                                = (FCBlock *)(((char *)fcb) + sizeof(FCBlock));

                        if(i == NON_MAC_QUEUE)
                                fcb->trc_next_ptr
                                        = RX_FCB_TRC_POINTER(fcb->next_ptr);
                        else
                                fcb->trc_next_ptr
                                        = TRC_POINTER(fcb->next_ptr);
                }

                fcb->next_ptr = tp->rx_fcb_head[i];

                if(i == NON_MAC_QUEUE)
                        fcb->trc_next_ptr = RX_FCB_TRC_POINTER(fcb->next_ptr);
                else
                        fcb->trc_next_ptr = TRC_POINTER(fcb->next_ptr);

                tp->rx_fcb_head[i]->back_ptr    = fcb;
                tp->rx_fcb_curr[i]              = tp->rx_fcb_head[i]->next_ptr;
        }

        return(0);
}

static int smctr_init_shared_memory(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i;
        __u32 *iscpb;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_init_shared_memory\n", dev->name);

        smctr_set_page(dev, (__u8 *)(unsigned int)tp->iscpb_ptr);

        /* Initialize Initial System Configuration Point. (ISCP) */
        iscpb = (__u32 *)PAGE_POINTER(&tp->iscpb_ptr->trc_scgb_ptr);
        *iscpb = (__u32)(SWAP_WORDS(TRC_POINTER(tp->scgb_ptr)));

        smctr_set_page(dev, (__u8 *)tp->ram_access);

        /* Initialize System Configuration Pointers. (SCP) */
        tp->scgb_ptr->config = (SCGB_ADDRESS_POINTER_FORMAT
                | SCGB_MULTI_WORD_CONTROL | SCGB_DATA_FORMAT
                | SCGB_BURST_LENGTH);

        tp->scgb_ptr->trc_sclb_ptr      = TRC_POINTER(tp->sclb_ptr);
        tp->scgb_ptr->trc_acb_ptr       = TRC_POINTER(tp->acb_head);
        tp->scgb_ptr->trc_isb_ptr       = TRC_POINTER(tp->isb_ptr);
        tp->scgb_ptr->isbsiz            = (sizeof(ISBlock)) - 2;

        /* Initialize System Control Block. (SCB) */
        tp->sclb_ptr->valid_command    = SCLB_VALID | SCLB_CMD_NOP;
        tp->sclb_ptr->iack_code        = 0;
        tp->sclb_ptr->resume_control   = 0;
        tp->sclb_ptr->int_mask_control = 0;
        tp->sclb_ptr->int_mask_state   = 0;

        /* Initialize Interrupt Status Block. (ISB) */
        for(i = 0; i < NUM_OF_INTERRUPTS; i++)
        {
                tp->isb_ptr->IStatus[i].IType = 0xf0;
                tp->isb_ptr->IStatus[i].ISubtype = 0;
        }

        tp->current_isb_index = 0;

        /* Initialize Action Command Block. (ACB) */
        smctr_init_acbs(dev);

        /* Initialize transmit FCB's and BDB's. */
        smctr_link_tx_fcbs_to_bdbs(dev);
        smctr_init_tx_bdbs(dev);
        smctr_init_tx_fcbs(dev);

        /* Initialize receive FCB's and BDB's. */
        smctr_init_rx_bdbs(dev);
        smctr_init_rx_fcbs(dev);

        return (0);
}

static int smctr_init_tx_bdbs(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i, j;
        BDBlock *bdb;

        for(i = 0; i < NUM_TX_QS_USED; i++)
        {
                bdb = tp->tx_bdb_head[i];
                bdb->info = (BDB_NOT_CHAIN_END | BDB_NO_WARNING);
                bdb->next_ptr = (BDBlock *)(((char *)bdb) + sizeof(BDBlock));
                bdb->trc_next_ptr = TRC_POINTER(bdb->next_ptr);

                for(j = 1; j < tp->num_tx_bdbs[i]; j++)
                {
                        bdb->next_ptr->back_ptr = bdb;
                        bdb = bdb->next_ptr;
                        bdb->info = (BDB_NOT_CHAIN_END | BDB_NO_WARNING);
                        bdb->next_ptr
                                = (BDBlock *)(((char *)bdb) + sizeof( BDBlock));                        bdb->trc_next_ptr = TRC_POINTER(bdb->next_ptr);
                }

                bdb->next_ptr = tp->tx_bdb_head[i];
                bdb->trc_next_ptr = TRC_POINTER(tp->tx_bdb_head[i]);
                tp->tx_bdb_head[i]->back_ptr = bdb;
        }

        return (0);
}

static int smctr_init_tx_fcbs(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i, j;
        FCBlock *fcb;

        for(i = 0; i < NUM_TX_QS_USED; i++)
        {
                fcb               = tp->tx_fcb_head[i];
                fcb->frame_status = 0;
                fcb->frame_length = 0;
                fcb->info         = FCB_CHAIN_END;
                fcb->next_ptr = (FCBlock *)(((char *)fcb) + sizeof(FCBlock));
                fcb->trc_next_ptr = TRC_POINTER(fcb->next_ptr);

                for(j = 1; j < tp->num_tx_fcbs[i]; j++)
                {
                        fcb->next_ptr->back_ptr = fcb;
                        fcb                     = fcb->next_ptr;
                        fcb->frame_status       = 0;
                        fcb->frame_length       = 0;
                        fcb->info               = FCB_CHAIN_END;
                        fcb->next_ptr
                                = (FCBlock *)(((char *)fcb) + sizeof(FCBlock));
                        fcb->trc_next_ptr = TRC_POINTER(fcb->next_ptr);
                }

                fcb->next_ptr           = tp->tx_fcb_head[i];
                fcb->trc_next_ptr       = TRC_POINTER(tp->tx_fcb_head[i]);

                tp->tx_fcb_head[i]->back_ptr    = fcb;
                tp->tx_fcb_end[i]               = tp->tx_fcb_head[i]->next_ptr;
                tp->tx_fcb_curr[i]              = tp->tx_fcb_head[i]->next_ptr;
                tp->num_tx_fcbs_used[i]         = 0;
        }

        return (0);
}

static int smctr_internal_self_test(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int err;

        if((err = smctr_issue_test_internal_rom_cmd(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        if(tp->acb_head->cmd_done_status & 0xff)
                return (-1);

        if((err = smctr_issue_test_hic_cmd(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        if(tp->acb_head->cmd_done_status & 0xff)
                return (-1);

        if((err = smctr_issue_test_mac_reg_cmd(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        if(tp->acb_head->cmd_done_status & 0xff)
                return (-1);

        return (0);
}

/*
 * The typical workload of the driver: Handle the network interface interrupts.
 */
static irqreturn_t smctr_interrupt(int irq, void *dev_id)
{
        struct net_device *dev = dev_id;
        struct net_local *tp;
        int ioaddr;
        __u16 interrupt_unmask_bits = 0, interrupt_ack_code = 0xff00;
        __u16 err1, err = NOT_MY_INTERRUPT;
        __u8 isb_type, isb_subtype;
        __u16 isb_index;

        ioaddr = dev->base_addr;
        tp = netdev_priv(dev);

        if(tp->status == NOT_INITIALIZED)
                return IRQ_NONE;

        spin_lock(&tp->lock);
        
        smctr_disable_bic_int(dev);
        smctr_enable_16bit(dev);

        smctr_clear_int(dev);

        /* First read the LSB */
        while((tp->isb_ptr->IStatus[tp->current_isb_index].IType & 0xf0) == 0)
        {
                isb_index       = tp->current_isb_index;
                isb_type        = tp->isb_ptr->IStatus[isb_index].IType;
                isb_subtype     = tp->isb_ptr->IStatus[isb_index].ISubtype;

                (tp->current_isb_index)++;
                if(tp->current_isb_index == NUM_OF_INTERRUPTS)
                        tp->current_isb_index = 0;

                if(isb_type >= 0x10)
                {
                        smctr_disable_16bit(dev);
		        spin_unlock(&tp->lock);
                        return IRQ_HANDLED;
                }

                err = HARDWARE_FAILED;
                interrupt_ack_code = isb_index;
                tp->isb_ptr->IStatus[isb_index].IType |= 0xf0;

                interrupt_unmask_bits |= (1 << (__u16)isb_type);

                switch(isb_type)
                {
                        case ISB_IMC_MAC_TYPE_3:
                                smctr_disable_16bit(dev);

                                switch(isb_subtype)
                                {
                                        case 0:
                                                tp->monitor_state = MS_MONITOR_FSM_INACTIVE;
                                               break;

                                        case 1:
                                                tp->monitor_state = MS_REPEAT_BEACON_STATE;
                                                break;

                                        case 2:
                                                tp->monitor_state = MS_REPEAT_CLAIM_TOKEN_STATE;
                                                break;

                                        case 3:
                                                tp->monitor_state = MS_TRANSMIT_CLAIM_TOKEN_STATE;                                                break;

                                        case 4:
                                                tp->monitor_state = MS_STANDBY_MONITOR_STATE;
                                                break;

                                        case 5:
                                                tp->monitor_state = MS_TRANSMIT_BEACON_STATE;
                                                break;

                                        case 6:
                                                tp->monitor_state = MS_ACTIVE_MONITOR_STATE;
                                                break;

                                        case 7:
                                                tp->monitor_state = MS_TRANSMIT_RING_PURGE_STATE;
                                                break;

                                        case 8:   /* diagnostic state */
                                                break;

                                        case 9:
                                                tp->monitor_state = MS_BEACON_TEST_STATE;
                                                if(smctr_lobe_media_test(dev))
                                                {
                                                        tp->ring_status_flags = RING_STATUS_CHANGED;
                                                        tp->ring_status = AUTO_REMOVAL_ERROR;
                                                        smctr_ring_status_chg(dev);
                                                        smctr_bypass_state(dev);
                                                }
                                                else
                                                        smctr_issue_insert_cmd(dev);
                                                break;

                                        /* case 0x0a-0xff, illegal states */
                                        default:
                                                break;
                                }

                                tp->ring_status_flags = MONITOR_STATE_CHANGED;
                                err = smctr_ring_status_chg(dev);

                                smctr_enable_16bit(dev);
                                break;

                        /* Type 0x02 - MAC Error Counters Interrupt
                         * One or more MAC Error Counter is half full
                         *      MAC Error Counters
                         *      Lost_FR_Error_Counter
                         *      RCV_Congestion_Counter
                         *      FR_copied_Error_Counter
                         *      FREQ_Error_Counter
                         *      Token_Error_Counter
                         *      Line_Error_Counter
                         *      Internal_Error_Count
                         */
                        case ISB_IMC_MAC_ERROR_COUNTERS:
                                /* Read 802.5 Error Counters */
                                err = smctr_issue_read_ring_status_cmd(dev);
                                break;

                        /* Type 0x04 - MAC Type 2 Interrupt
                         * HOST needs to enqueue MAC Frame for transmission
                         * SubType Bit 15 - RQ_INIT_PDU( Request Initialization)                         * Changed from RQ_INIT_PDU to
                         * TRC_Status_Changed_Indicate
                         */
                        case ISB_IMC_MAC_TYPE_2:
                                err = smctr_issue_read_ring_status_cmd(dev);
                                break;


                        /* Type 0x05 - TX Frame Interrupt (FI). */
                        case ISB_IMC_TX_FRAME:
                                /* BUG QUEUE for TRC stuck receive BUG */
                                if(isb_subtype & TX_PENDING_PRIORITY_2)
                                {
                                        if((err = smctr_tx_complete(dev, BUG_QUEUE)) != SUCCESS)
                                                break;
                                }

                                /* NON-MAC frames only */
                                if(isb_subtype & TX_PENDING_PRIORITY_1)
                                {
                                        if((err = smctr_tx_complete(dev, NON_MAC_QUEUE)) != SUCCESS)
                                                break;
                                }

                                /* MAC frames only */
                                if(isb_subtype & TX_PENDING_PRIORITY_0)
                                        err = smctr_tx_complete(dev, MAC_QUEUE);                                break;

                        /* Type 0x06 - TX END OF QUEUE (FE) */
                        case ISB_IMC_END_OF_TX_QUEUE:
                                /* BUG queue */
                                if(isb_subtype & TX_PENDING_PRIORITY_2)
                                {
                                        /* ok to clear Receive FIFO overrun
                                         * imask send_BUG now completes.
                                         */
                                        interrupt_unmask_bits |= 0x800;

                                        tp->tx_queue_status[BUG_QUEUE] = NOT_TRANSMITING;
                                        if((err = smctr_tx_complete(dev, BUG_QUEUE)) != SUCCESS)
                                                break;
                                        if((err = smctr_restart_tx_chain(dev, BUG_QUEUE)) != SUCCESS)
                                                break;
                                }

                                /* NON-MAC queue only */
                                if(isb_subtype & TX_PENDING_PRIORITY_1)
                                {
                                        tp->tx_queue_status[NON_MAC_QUEUE] = NOT_TRANSMITING;
                                        if((err = smctr_tx_complete(dev, NON_MAC_QUEUE)) != SUCCESS)
                                                break;
                                        if((err = smctr_restart_tx_chain(dev, NON_MAC_QUEUE)) != SUCCESS)
                                                break;
                                }

                                /* MAC queue only */
                                if(isb_subtype & TX_PENDING_PRIORITY_0)
                                {
                                        tp->tx_queue_status[MAC_QUEUE] = NOT_TRANSMITING;
                                        if((err = smctr_tx_complete(dev, MAC_QUEUE)) != SUCCESS)
                                                break;

                                        err = smctr_restart_tx_chain(dev, MAC_QUEUE);
                                }
                                break;

                        /* Type 0x07 - NON-MAC RX Resource Interrupt
                         *   Subtype bit 12 - (BW) BDB warning
                         *   Subtype bit 13 - (FW) FCB warning
                         *   Subtype bit 14 - (BE) BDB End of chain
                         *   Subtype bit 15 - (FE) FCB End of chain
                         */
                        case ISB_IMC_NON_MAC_RX_RESOURCE:
                                tp->rx_fifo_overrun_count = 0;
                                tp->receive_queue_number = NON_MAC_QUEUE;
                                err1 = smctr_rx_frame(dev);

                                if(isb_subtype & NON_MAC_RX_RESOURCE_FE)
                                {
                                        if((err = smctr_issue_resume_rx_fcb_cmd(                                                dev, NON_MAC_QUEUE)) != SUCCESS)                                                break;

                                        if(tp->ptr_rx_fcb_overruns)
                                                (*tp->ptr_rx_fcb_overruns)++;
                                }

                                if(isb_subtype & NON_MAC_RX_RESOURCE_BE)
                                {
                                        if((err = smctr_issue_resume_rx_bdb_cmd(                                                dev, NON_MAC_QUEUE)) != SUCCESS)                                                break;

                                        if(tp->ptr_rx_bdb_overruns)
                                                (*tp->ptr_rx_bdb_overruns)++;
                                }
                                err = err1;
                                break;

                        /* Type 0x08 - MAC RX Resource Interrupt
                         *   Subtype bit 12 - (BW) BDB warning
                         *   Subtype bit 13 - (FW) FCB warning
                         *   Subtype bit 14 - (BE) BDB End of chain
                         *   Subtype bit 15 - (FE) FCB End of chain
                         */
                        case ISB_IMC_MAC_RX_RESOURCE:
                                tp->receive_queue_number = MAC_QUEUE;
                                err1 = smctr_rx_frame(dev);

                                if(isb_subtype & MAC_RX_RESOURCE_FE)
                                {
                                        if((err = smctr_issue_resume_rx_fcb_cmd(                                                dev, MAC_QUEUE)) != SUCCESS)
                                                break;

                                        if(tp->ptr_rx_fcb_overruns)
                                                (*tp->ptr_rx_fcb_overruns)++;
                                }

                                if(isb_subtype & MAC_RX_RESOURCE_BE)
                                {
                                        if((err = smctr_issue_resume_rx_bdb_cmd(                                                dev, MAC_QUEUE)) != SUCCESS)
                                                break;

                                        if(tp->ptr_rx_bdb_overruns)
                                                (*tp->ptr_rx_bdb_overruns)++;
                                }
                                err = err1;
                                break;

                        /* Type 0x09 - NON_MAC RX Frame Interrupt */
                        case ISB_IMC_NON_MAC_RX_FRAME:
                                tp->rx_fifo_overrun_count = 0;
                                tp->receive_queue_number = NON_MAC_QUEUE;
                                err = smctr_rx_frame(dev);
                                break;

                        /* Type 0x0A - MAC RX Frame Interrupt */
                        case ISB_IMC_MAC_RX_FRAME:
                                tp->receive_queue_number = MAC_QUEUE;
                                err = smctr_rx_frame(dev);
                                break;

                        /* Type 0x0B - TRC status
                         * TRC has encountered an error condition
                         * subtype bit 14 - transmit FIFO underrun
                         * subtype bit 15 - receive FIFO overrun
                         */
                        case ISB_IMC_TRC_FIFO_STATUS:
                                if(isb_subtype & TRC_FIFO_STATUS_TX_UNDERRUN)
                                {
                                        if(tp->ptr_tx_fifo_underruns)
                                                (*tp->ptr_tx_fifo_underruns)++;
                                }

                                if(isb_subtype & TRC_FIFO_STATUS_RX_OVERRUN)
                                {
                                        /* update overrun stuck receive counter
                                         * if >= 3, has to clear it by sending
                                         * back to back frames. We pick
                                         * DAT(duplicate address MAC frame)
                                         */
                                        tp->rx_fifo_overrun_count++;

                                        if(tp->rx_fifo_overrun_count >= 3)
                                        {
                                                tp->rx_fifo_overrun_count = 0;

                                                /* delay clearing fifo overrun
                                                 * imask till send_BUG tx
                                                 * complete posted
                                                 */
                                                interrupt_unmask_bits &= (~0x800);
                                                printk(KERN_CRIT "Jay please send bug\n");//                                              smctr_send_bug(dev);
                                        }

                                        if(tp->ptr_rx_fifo_overruns)
                                                (*tp->ptr_rx_fifo_overruns)++;
                                }

                                err = SUCCESS;
                                break;

                        /* Type 0x0C - Action Command Status Interrupt
                         * Subtype bit 14 - CB end of command chain (CE)
                         * Subtype bit 15 - CB command interrupt (CI)
                         */
                        case ISB_IMC_COMMAND_STATUS:
                                err = SUCCESS;
                                if(tp->acb_head->cmd == ACB_CMD_HIC_NOP)
                                {
                                        printk(KERN_ERR "i1\n");
                                        smctr_disable_16bit(dev);

                                        /* XXXXXXXXXXXXXXXXX */
                                /*      err = UM_Interrupt(dev); */

                                        smctr_enable_16bit(dev);
                                }
                                else
                                {
                                        if((tp->acb_head->cmd
					    == ACB_CMD_READ_TRC_STATUS) &&
					   (tp->acb_head->subcmd
					    == RW_TRC_STATUS_BLOCK))
                                        {
                                                if(tp->ptr_bcn_type)
                                                {
                                                        *(tp->ptr_bcn_type)
                                                                = (__u32)((SBlock *)tp->misc_command_data)->BCN_Type;
                                                }

                                                if(((SBlock *)tp->misc_command_data)->Status_CHG_Indicate & ERROR_COUNTERS_CHANGED)
                                                {
                                                        smctr_update_err_stats(dev);
                                                }

                                                if(((SBlock *)tp->misc_command_data)->Status_CHG_Indicate & TI_NDIS_RING_STATUS_CHANGED)
                                                {
                                                        tp->ring_status
                                                                = ((SBlock*)tp->misc_command_data)->TI_NDIS_Ring_Status;
                                                        smctr_disable_16bit(dev);
                                                        err = smctr_ring_status_chg(dev);
                                                        smctr_enable_16bit(dev);
                                                        if((tp->ring_status & REMOVE_RECEIVED) &&
							   (tp->config_word0 & NO_AUTOREMOVE))
                                                        {
                                                                smctr_issue_remove_cmd(dev);
                                                        }

                                                        if(err != SUCCESS)
                                                        {
                                                                tp->acb_pending = 0;
                                                                break;
                                                        }
                                                }

                                                if(((SBlock *)tp->misc_command_data)->Status_CHG_Indicate & UNA_CHANGED)
                                                {
                                                        if(tp->ptr_una)
                                                        {
                                                                tp->ptr_una[0] = SWAP_BYTES(((SBlock *)tp->misc_command_data)->UNA[0]);
                                                                tp->ptr_una[1] = SWAP_BYTES(((SBlock *)tp->misc_command_data)->UNA[1]);
                                                                tp->ptr_una[2] = SWAP_BYTES(((SBlock *)tp->misc_command_data)->UNA[2]);
                                                        }

                                                }

                                                if(((SBlock *)tp->misc_command_data)->Status_CHG_Indicate & READY_TO_SEND_RQ_INIT)                                                {
                                                        err = smctr_send_rq_init(dev);
                                                }
                                        }
                                }

                                tp->acb_pending = 0;
                                break;

                        /* Type 0x0D - MAC Type 1 interrupt
                         * Subtype -- 00 FR_BCN received at S12
                         *            01 FR_BCN received at S21
                         *            02 FR_DAT(DA=MA, A<>0) received at S21
                         *            03 TSM_EXP at S21
                         *            04 FR_REMOVE received at S42
                         *            05 TBR_EXP, BR_FLAG_SET at S42
                         *            06 TBT_EXP at S53
                         */
                        case ISB_IMC_MAC_TYPE_1:
                                if(isb_subtype > 8)
                                {
                                        err = HARDWARE_FAILED;
                                        break;
                                }

                                err = SUCCESS;
                                switch(isb_subtype)
                                {
                                        case 0:
                                                tp->join_state = JS_BYPASS_STATE;
                                                if(tp->status != CLOSED)
                                                {
                                                        tp->status = CLOSED;
                                                        err = smctr_status_chg(dev);
                                                }
                                                break;

                                        case 1:
                                                tp->join_state = JS_LOBE_TEST_STATE;
                                                break;

                                        case 2:
                                                tp->join_state = JS_DETECT_MONITOR_PRESENT_STATE;
                                                break;

                                        case 3:
                                                tp->join_state = JS_AWAIT_NEW_MONITOR_STATE;
                                                break;

                                        case 4:
                                                tp->join_state = JS_DUPLICATE_ADDRESS_TEST_STATE;
                                                break;

                                        case 5:
                                                tp->join_state = JS_NEIGHBOR_NOTIFICATION_STATE;
                                                break;

                                        case 6:
                                                tp->join_state = JS_REQUEST_INITIALIZATION_STATE;
                                                break;

                                        case 7:
                                                tp->join_state = JS_JOIN_COMPLETE_STATE;
                                                tp->status = OPEN;
                                                err = smctr_status_chg(dev);
                                                break;

                                        case 8:
                                                tp->join_state = JS_BYPASS_WAIT_STATE;
                                                break;
                                }
                                break ;

                        /* Type 0x0E - TRC Initialization Sequence Interrupt
                         * Subtype -- 00-FF Initializatin sequence complete
                         */
                        case ISB_IMC_TRC_INTRNL_TST_STATUS:
                                tp->status = INITIALIZED;
                                smctr_disable_16bit(dev);
                                err = smctr_status_chg(dev);
                                smctr_enable_16bit(dev);
                                break;

                        /* other interrupt types, illegal */
                        default:
                                break;
                }

                if(err != SUCCESS)
                        break;
        }

        /* Checking the ack code instead of the unmask bits here is because :
         * while fixing the stuck receive, DAT frame are sent and mask off
         * FIFO overrun interrupt temporarily (interrupt_unmask_bits = 0)
         * but we still want to issue ack to ISB
         */
        if(!(interrupt_ack_code & 0xff00))
                smctr_issue_int_ack(dev, interrupt_ack_code, interrupt_unmask_bits);

        smctr_disable_16bit(dev);
        smctr_enable_bic_int(dev);
        spin_unlock(&tp->lock);

        return IRQ_HANDLED;
}

static int smctr_issue_enable_int_cmd(struct net_device *dev,
        __u16 interrupt_enable_mask)
{
        struct net_local *tp = netdev_priv(dev);
        int err;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        tp->sclb_ptr->int_mask_control  = interrupt_enable_mask;
        tp->sclb_ptr->valid_command     = SCLB_VALID | SCLB_CMD_CLEAR_INTERRUPT_MASK;

        smctr_set_ctrl_attention(dev);

        return (0);
}

static int smctr_issue_int_ack(struct net_device *dev, __u16 iack_code, __u16 ibits)
{
        struct net_local *tp = netdev_priv(dev);

        if(smctr_wait_while_cbusy(dev))
                return (-1);

        tp->sclb_ptr->int_mask_control = ibits;
        tp->sclb_ptr->iack_code = iack_code << 1; /* use the offset from base */        tp->sclb_ptr->resume_control = 0;
        tp->sclb_ptr->valid_command = SCLB_VALID | SCLB_IACK_CODE_VALID | SCLB_CMD_CLEAR_INTERRUPT_MASK;

        smctr_set_ctrl_attention(dev);

        return (0);
}

static int smctr_issue_init_timers_cmd(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i;
        int err;
        __u16 *pTimer_Struc = (__u16 *)tp->misc_command_data;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        tp->config_word0 = THDREN | DMA_TRIGGER | USETPT | NO_AUTOREMOVE;
        tp->config_word1 = 0;

        if((tp->media_type == MEDIA_STP_16) ||
	   (tp->media_type == MEDIA_UTP_16) ||
	   (tp->media_type == MEDIA_STP_16_UTP_16))
        {
                tp->config_word0 |= FREQ_16MB_BIT;
        }

        if(tp->mode_bits & EARLY_TOKEN_REL)
                tp->config_word0 |= ETREN;

        if(tp->mode_bits & LOOPING_MODE_MASK)
                tp->config_word0 |= RX_OWN_BIT;
        else
                tp->config_word0 &= ~RX_OWN_BIT;

        if(tp->receive_mask & PROMISCUOUS_MODE)
                tp->config_word0 |= PROMISCUOUS_BIT;
        else
                tp->config_word0 &= ~PROMISCUOUS_BIT;

        if(tp->receive_mask & ACCEPT_ERR_PACKETS)
                tp->config_word0 |= SAVBAD_BIT;
        else
                tp->config_word0 &= ~SAVBAD_BIT;

        if(tp->receive_mask & ACCEPT_ATT_MAC_FRAMES)
                tp->config_word0 |= RXATMAC;
        else
                tp->config_word0 &= ~RXATMAC;

        if(tp->receive_mask & ACCEPT_MULTI_PROM)
                tp->config_word1 |= MULTICAST_ADDRESS_BIT;
        else
                tp->config_word1 &= ~MULTICAST_ADDRESS_BIT;

        if(tp->receive_mask & ACCEPT_SOURCE_ROUTING_SPANNING)
                tp->config_word1 |= SOURCE_ROUTING_SPANNING_BITS;
        else
        {
                if(tp->receive_mask & ACCEPT_SOURCE_ROUTING)
                        tp->config_word1 |= SOURCE_ROUTING_EXPLORER_BIT;
                else
                        tp->config_word1 &= ~SOURCE_ROUTING_SPANNING_BITS;
        }

        if((tp->media_type == MEDIA_STP_16) ||
	   (tp->media_type == MEDIA_UTP_16) ||
	   (tp->media_type == MEDIA_STP_16_UTP_16))
        {
                tp->config_word1 |= INTERFRAME_SPACING_16;
        }
        else
                tp->config_word1 |= INTERFRAME_SPACING_4;

        *pTimer_Struc++ = tp->config_word0;
        *pTimer_Struc++ = tp->config_word1;

        if((tp->media_type == MEDIA_STP_4) ||
	   (tp->media_type == MEDIA_UTP_4) ||
	   (tp->media_type == MEDIA_STP_4_UTP_4))
        {
                *pTimer_Struc++ = 0x00FA;       /* prescale */
                *pTimer_Struc++ = 0x2710;       /* TPT_limit */
                *pTimer_Struc++ = 0x2710;       /* TQP_limit */
                *pTimer_Struc++ = 0x0A28;       /* TNT_limit */
                *pTimer_Struc++ = 0x3E80;       /* TBT_limit */
                *pTimer_Struc++ = 0x3A98;       /* TSM_limit */
                *pTimer_Struc++ = 0x1B58;       /* TAM_limit */
                *pTimer_Struc++ = 0x00C8;       /* TBR_limit */
                *pTimer_Struc++ = 0x07D0;       /* TER_limit */
                *pTimer_Struc++ = 0x000A;       /* TGT_limit */
                *pTimer_Struc++ = 0x1162;       /* THT_limit */
                *pTimer_Struc++ = 0x07D0;       /* TRR_limit */
                *pTimer_Struc++ = 0x1388;       /* TVX_limit */
                *pTimer_Struc++ = 0x0000;       /* reserved */
        }
        else
        {
                *pTimer_Struc++ = 0x03E8;       /* prescale */
                *pTimer_Struc++ = 0x9C40;       /* TPT_limit */
                *pTimer_Struc++ = 0x9C40;       /* TQP_limit */
                *pTimer_Struc++ = 0x0A28;       /* TNT_limit */
                *pTimer_Struc++ = 0x3E80;       /* TBT_limit */
                *pTimer_Struc++ = 0x3A98;       /* TSM_limit */
                *pTimer_Struc++ = 0x1B58;       /* TAM_limit */
                *pTimer_Struc++ = 0x00C8;       /* TBR_limit */
                *pTimer_Struc++ = 0x07D0;       /* TER_limit */
                *pTimer_Struc++ = 0x000A;       /* TGT_limit */
                *pTimer_Struc++ = 0x4588;       /* THT_limit */
                *pTimer_Struc++ = 0x1F40;       /* TRR_limit */
                *pTimer_Struc++ = 0x4E20;       /* TVX_limit */
                *pTimer_Struc++ = 0x0000;       /* reserved */
        }

        /* Set node address. */
        *pTimer_Struc++ = dev->dev_addr[0] << 8
                | (dev->dev_addr[1] & 0xFF);
        *pTimer_Struc++ = dev->dev_addr[2] << 8
                | (dev->dev_addr[3] & 0xFF);
        *pTimer_Struc++ = dev->dev_addr[4] << 8
                | (dev->dev_addr[5] & 0xFF);

        /* Set group address. */
        *pTimer_Struc++ = tp->group_address_0 << 8
                | tp->group_address_0 >> 8;
        *pTimer_Struc++ = tp->group_address[0] << 8
                | tp->group_address[0] >> 8;
        *pTimer_Struc++ = tp->group_address[1] << 8
                | tp->group_address[1] >> 8;

        /* Set functional address. */
        *pTimer_Struc++ = tp->functional_address_0 << 8
                | tp->functional_address_0 >> 8;
        *pTimer_Struc++ = tp->functional_address[0] << 8
                | tp->functional_address[0] >> 8;
        *pTimer_Struc++ = tp->functional_address[1] << 8
                | tp->functional_address[1] >> 8;

        /* Set Bit-Wise group address. */
        *pTimer_Struc++ = tp->bitwise_group_address[0] << 8
                | tp->bitwise_group_address[0] >> 8;
        *pTimer_Struc++ = tp->bitwise_group_address[1] << 8
                | tp->bitwise_group_address[1] >> 8;

        /* Set ring number address. */
        *pTimer_Struc++ = tp->source_ring_number;
        *pTimer_Struc++ = tp->target_ring_number;

        /* Physical drop number. */
        *pTimer_Struc++ = (unsigned short)0;
        *pTimer_Struc++ = (unsigned short)0;

        /* Product instance ID. */
        for(i = 0; i < 9; i++)
                *pTimer_Struc++ = (unsigned short)0;

        err = smctr_setup_single_cmd_w_data(dev, ACB_CMD_INIT_TRC_TIMERS, 0);

        return (err);
}

static int smctr_issue_init_txrx_cmd(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i;
        int err;
        void **txrx_ptrs = (void *)tp->misc_command_data;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
	{
                printk(KERN_ERR "%s: Hardware failure\n", dev->name);
                return (err);
        }

        /* Initialize Transmit Queue Pointers that are used, to point to
         * a single FCB.
         */
        for(i = 0; i < NUM_TX_QS_USED; i++)
                *txrx_ptrs++ = (void *)TRC_POINTER(tp->tx_fcb_head[i]);

        /* Initialize Transmit Queue Pointers that are NOT used to ZERO. */
        for(; i < MAX_TX_QS; i++)
                *txrx_ptrs++ = (void *)0;

        /* Initialize Receive Queue Pointers (MAC and Non-MAC) that are
         * used, to point to a single FCB and a BDB chain of buffers.
         */
        for(i = 0; i < NUM_RX_QS_USED; i++)
        {
                *txrx_ptrs++ = (void *)TRC_POINTER(tp->rx_fcb_head[i]);
                *txrx_ptrs++ = (void *)TRC_POINTER(tp->rx_bdb_head[i]);
        }

        /* Initialize Receive Queue Pointers that are NOT used to ZERO. */
        for(; i < MAX_RX_QS; i++)
        {
                *txrx_ptrs++ = (void *)0;
                *txrx_ptrs++ = (void *)0;
        }

        err = smctr_setup_single_cmd_w_data(dev, ACB_CMD_INIT_TX_RX, 0);

        return (err);
}

static int smctr_issue_insert_cmd(struct net_device *dev)
{
        int err;

        err = smctr_setup_single_cmd(dev, ACB_CMD_INSERT, ACB_SUB_CMD_NOP);

        return (err);
}

static int smctr_issue_read_ring_status_cmd(struct net_device *dev)
{
        int err;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        err = smctr_setup_single_cmd_w_data(dev, ACB_CMD_READ_TRC_STATUS,
                RW_TRC_STATUS_BLOCK);

        return (err);
}

static int smctr_issue_read_word_cmd(struct net_device *dev, __u16 aword_cnt)
{
        int err;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        err = smctr_setup_single_cmd_w_data(dev, ACB_CMD_MCT_READ_VALUE,
                aword_cnt);

        return (err);
}

static int smctr_issue_remove_cmd(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int err;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        tp->sclb_ptr->resume_control    = 0;
        tp->sclb_ptr->valid_command     = SCLB_VALID | SCLB_CMD_REMOVE;

        smctr_set_ctrl_attention(dev);

        return (0);
}

static int smctr_issue_resume_acb_cmd(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int err;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        tp->sclb_ptr->resume_control = SCLB_RC_ACB;
        tp->sclb_ptr->valid_command  = SCLB_VALID | SCLB_RESUME_CONTROL_VALID;

        tp->acb_pending = 1;

        smctr_set_ctrl_attention(dev);

        return (0);
}

static int smctr_issue_resume_rx_bdb_cmd(struct net_device *dev, __u16 queue)
{
        struct net_local *tp = netdev_priv(dev);
        int err;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        if(queue == MAC_QUEUE)
                tp->sclb_ptr->resume_control = SCLB_RC_RX_MAC_BDB;
        else
                tp->sclb_ptr->resume_control = SCLB_RC_RX_NON_MAC_BDB;

        tp->sclb_ptr->valid_command = SCLB_VALID | SCLB_RESUME_CONTROL_VALID;

        smctr_set_ctrl_attention(dev);

        return (0);
}

static int smctr_issue_resume_rx_fcb_cmd(struct net_device *dev, __u16 queue)
{
        struct net_local *tp = netdev_priv(dev);

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_issue_resume_rx_fcb_cmd\n", dev->name);

        if(smctr_wait_while_cbusy(dev))
                return (-1);

        if(queue == MAC_QUEUE)
                tp->sclb_ptr->resume_control = SCLB_RC_RX_MAC_FCB;
        else
                tp->sclb_ptr->resume_control = SCLB_RC_RX_NON_MAC_FCB;

        tp->sclb_ptr->valid_command = SCLB_VALID | SCLB_RESUME_CONTROL_VALID;

        smctr_set_ctrl_attention(dev);

        return (0);
}

static int smctr_issue_resume_tx_fcb_cmd(struct net_device *dev, __u16 queue)
{
        struct net_local *tp = netdev_priv(dev);

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_issue_resume_tx_fcb_cmd\n", dev->name);

        if(smctr_wait_while_cbusy(dev))
                return (-1);

        tp->sclb_ptr->resume_control = (SCLB_RC_TFCB0 << queue);
        tp->sclb_ptr->valid_command = SCLB_RESUME_CONTROL_VALID | SCLB_VALID;

        smctr_set_ctrl_attention(dev);

        return (0);
}

static int smctr_issue_test_internal_rom_cmd(struct net_device *dev)
{
        int err;

        err = smctr_setup_single_cmd(dev, ACB_CMD_MCT_TEST,
                TRC_INTERNAL_ROM_TEST);

        return (err);
}

static int smctr_issue_test_hic_cmd(struct net_device *dev)
{
        int err;

        err = smctr_setup_single_cmd(dev, ACB_CMD_HIC_TEST,
                TRC_HOST_INTERFACE_REG_TEST);

        return (err);
}

static int smctr_issue_test_mac_reg_cmd(struct net_device *dev)
{
        int err;

        err = smctr_setup_single_cmd(dev, ACB_CMD_MCT_TEST,
                TRC_MAC_REGISTERS_TEST);

        return (err);
}

static int smctr_issue_trc_loopback_cmd(struct net_device *dev)
{
        int err;

        err = smctr_setup_single_cmd(dev, ACB_CMD_MCT_TEST,
                TRC_INTERNAL_LOOPBACK);

        return (err);
}

static int smctr_issue_tri_loopback_cmd(struct net_device *dev)
{
        int err;

        err = smctr_setup_single_cmd(dev, ACB_CMD_MCT_TEST,
                TRC_TRI_LOOPBACK);

        return (err);
}

static int smctr_issue_write_byte_cmd(struct net_device *dev,
        short aword_cnt, void *byte)
{
	struct net_local *tp = netdev_priv(dev);
        unsigned int iword, ibyte;
	int err;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        for(iword = 0, ibyte = 0; iword < (unsigned int)(aword_cnt & 0xff);
        	iword++, ibyte += 2)
        {
                tp->misc_command_data[iword] = (*((__u8 *)byte + ibyte) << 8)
			| (*((__u8 *)byte + ibyte + 1));
        }

        return (smctr_setup_single_cmd_w_data(dev, ACB_CMD_MCT_WRITE_VALUE, 
		aword_cnt));
}

static int smctr_issue_write_word_cmd(struct net_device *dev,
        short aword_cnt, void *word)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i, err;

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        for(i = 0; i < (unsigned int)(aword_cnt & 0xff); i++)
                tp->misc_command_data[i] = *((__u16 *)word + i);

        err = smctr_setup_single_cmd_w_data(dev, ACB_CMD_MCT_WRITE_VALUE,
                aword_cnt);

        return (err);
}

static int smctr_join_complete_state(struct net_device *dev)
{
        int err;

        err = smctr_setup_single_cmd(dev, ACB_CMD_CHANGE_JOIN_STATE,
                JS_JOIN_COMPLETE_STATE);

        return (err);
}

static int smctr_link_tx_fcbs_to_bdbs(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i, j;
        FCBlock *fcb;
        BDBlock *bdb;

        for(i = 0; i < NUM_TX_QS_USED; i++)
        {
                fcb = tp->tx_fcb_head[i];
                bdb = tp->tx_bdb_head[i];

                for(j = 0; j < tp->num_tx_fcbs[i]; j++)
                {
                        fcb->bdb_ptr            = bdb;
                        fcb->trc_bdb_ptr        = TRC_POINTER(bdb);
                        fcb = (FCBlock *)((char *)fcb + sizeof(FCBlock));
                        bdb = (BDBlock *)((char *)bdb + sizeof(BDBlock));
                }
        }

        return (0);
}

static int smctr_load_firmware(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
	const struct firmware *fw;
        __u16 i, checksum = 0;
        int err = 0;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_load_firmware\n", dev->name);

	if (request_firmware(&fw, "tr_smctr.bin", &dev->dev)) {
		printk(KERN_ERR "%s: firmware not found\n", dev->name);
		return (UCODE_NOT_PRESENT);
	}

        tp->num_of_tx_buffs     = 4;
        tp->mode_bits          |= UMAC;
        tp->receive_mask        = 0;
        tp->max_packet_size     = 4177;

        /* Can only upload the firmware once per adapter reset. */
        if (tp->microcode_version != 0) {
		err = (UCODE_PRESENT);
		goto out;
	}

        /* Verify the firmware exists and is there in the right amount. */
        if (!fw->data ||
	    (*(fw->data + UCODE_VERSION_OFFSET) < UCODE_VERSION))
        {
                err = (UCODE_NOT_PRESENT);
		goto out;
        }

        /* UCODE_SIZE is not included in Checksum. */
        for(i = 0; i < *((__u16 *)(fw->data + UCODE_SIZE_OFFSET)); i += 2)
                checksum += *((__u16 *)(fw->data + 2 + i));
        if (checksum) {
		err = (UCODE_NOT_PRESENT);
		goto out;
	}

        /* At this point we have a valid firmware image, lets kick it on up. */
        smctr_enable_adapter_ram(dev);
        smctr_enable_16bit(dev);
        smctr_set_page(dev, (__u8 *)tp->ram_access);

        if((smctr_checksum_firmware(dev)) ||
	   (*(fw->data + UCODE_VERSION_OFFSET) > tp->microcode_version))
        {
                smctr_enable_adapter_ctrl_store(dev);

                /* Zero out ram space for firmware. */
                for(i = 0; i < CS_RAM_SIZE; i += 2)
                        *((__u16 *)(tp->ram_access + i)) = 0;

                smctr_decode_firmware(dev, fw);

                tp->microcode_version = *(fw->data + UCODE_VERSION_OFFSET);                *((__u16 *)(tp->ram_access + CS_RAM_VERSION_OFFSET))
                        = (tp->microcode_version << 8);
                *((__u16 *)(tp->ram_access + CS_RAM_CHECKSUM_OFFSET))
                        = ~(tp->microcode_version << 8) + 1;

                smctr_disable_adapter_ctrl_store(dev);

                if(smctr_checksum_firmware(dev))
                        err = HARDWARE_FAILED;
        }
        else
                err = UCODE_PRESENT;

        smctr_disable_16bit(dev);
 out:
	release_firmware(fw);
        return (err);
}

static int smctr_load_node_addr(struct net_device *dev)
{
        int ioaddr = dev->base_addr;
        unsigned int i;
        __u8 r;

        for(i = 0; i < 6; i++)
        {
                r = inb(ioaddr + LAR0 + i);
                dev->dev_addr[i] = (char)r;
        }
        dev->addr_len = 6;

        return (0);
}

/* Lobe Media Test.
 * During the transmission of the initial 1500 lobe media MAC frames,
 * the phase lock loop in the 805 chip may lock, and then un-lock, causing
 * the 825 to go into a PURGE state. When performing a PURGE, the MCT
 * microcode will not transmit any frames given to it by the host, and
 * will consequently cause a timeout.
 *
 * NOTE 1: If the monitor_state is MS_BEACON_TEST_STATE, all transmit
 * queues other than the one used for the lobe_media_test should be
 * disabled.!?
 *
 * NOTE 2: If the monitor_state is MS_BEACON_TEST_STATE and the receive_mask
 * has any multi-cast or promiscous bits set, the receive_mask needs to
 * be changed to clear the multi-cast or promiscous mode bits, the lobe_test
 * run, and then the receive mask set back to its original value if the test
 * is successful.
 */
static int smctr_lobe_media_test(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i, perror = 0;
        unsigned short saved_rcv_mask;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_lobe_media_test\n", dev->name);

        /* Clear receive mask for lobe test. */
        saved_rcv_mask          = tp->receive_mask;
        tp->receive_mask        = 0;

        smctr_chg_rx_mask(dev);

        /* Setup the lobe media test. */
        smctr_lobe_media_test_cmd(dev);
        if(smctr_wait_cmd(dev))
		goto err;

        /* Tx lobe media test frames. */
        for(i = 0; i < 1500; ++i)
        {
                if(smctr_send_lobe_media_test(dev))
                {
                        if(perror)
				goto err;
                        else
                        {
                                perror = 1;
                                if(smctr_lobe_media_test_cmd(dev))
					goto err;
                        }
                }
        }

        if(smctr_send_dat(dev))
        {
                if(smctr_send_dat(dev))
			goto err;
        }

        /* Check if any frames received during test. */
        if((tp->rx_fcb_curr[MAC_QUEUE]->frame_status) ||
	   (tp->rx_fcb_curr[NON_MAC_QUEUE]->frame_status))
		goto err;

        /* Set receive mask to "Promisc" mode. */
        tp->receive_mask = saved_rcv_mask;

        smctr_chg_rx_mask(dev);

	 return 0;
err:
	smctr_reset_adapter(dev);
	tp->status = CLOSED;
	return LOBE_MEDIA_TEST_FAILED;
}

static int smctr_lobe_media_test_cmd(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int err;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_lobe_media_test_cmd\n", dev->name);

        /* Change to lobe media test state. */
        if(tp->monitor_state != MS_BEACON_TEST_STATE)
        {
                smctr_lobe_media_test_state(dev);
                if(smctr_wait_cmd(dev))
                {
                        printk(KERN_ERR "Lobe Failed test state\n");
                        return (LOBE_MEDIA_TEST_FAILED);
                }
        }

        err = smctr_setup_single_cmd(dev, ACB_CMD_MCT_TEST,
                TRC_LOBE_MEDIA_TEST);

        return (err);
}

static int smctr_lobe_media_test_state(struct net_device *dev)
{
        int err;

        err = smctr_setup_single_cmd(dev, ACB_CMD_CHANGE_JOIN_STATE,
                JS_LOBE_TEST_STATE);

        return (err);
}

static int smctr_make_8025_hdr(struct net_device *dev,
        MAC_HEADER *rmf, MAC_HEADER *tmf, __u16 ac_fc)
{
        tmf->ac = MSB(ac_fc);                 /* msb is access control */
        tmf->fc = LSB(ac_fc);                 /* lsb is frame control */

        tmf->sa[0] = dev->dev_addr[0];
        tmf->sa[1] = dev->dev_addr[1];
        tmf->sa[2] = dev->dev_addr[2];
        tmf->sa[3] = dev->dev_addr[3];
        tmf->sa[4] = dev->dev_addr[4];
        tmf->sa[5] = dev->dev_addr[5];

        switch(tmf->vc)
        {
		/* Send RQ_INIT to RPS */
                case RQ_INIT:
                        tmf->da[0] = 0xc0;
                        tmf->da[1] = 0x00;
                        tmf->da[2] = 0x00;
                        tmf->da[3] = 0x00;
                        tmf->da[4] = 0x00;
                        tmf->da[5] = 0x02;
                        break;

		/* Send RPT_TX_FORWARD to CRS */
                case RPT_TX_FORWARD:
                        tmf->da[0] = 0xc0;
                        tmf->da[1] = 0x00;
                        tmf->da[2] = 0x00;
                        tmf->da[3] = 0x00;
                        tmf->da[4] = 0x00;
                        tmf->da[5] = 0x10;
                        break;

		/* Everything else goes to sender */
                default:
                        tmf->da[0] = rmf->sa[0];
                        tmf->da[1] = rmf->sa[1];
                        tmf->da[2] = rmf->sa[2];
                        tmf->da[3] = rmf->sa[3];
                        tmf->da[4] = rmf->sa[4];
                        tmf->da[5] = rmf->sa[5];
                        break;
        }

        return (0);
}

static int smctr_make_access_pri(struct net_device *dev, MAC_SUB_VECTOR *tsv)
{
        struct net_local *tp = netdev_priv(dev);

        tsv->svi = AUTHORIZED_ACCESS_PRIORITY;
        tsv->svl = S_AUTHORIZED_ACCESS_PRIORITY;

        tsv->svv[0] = MSB(tp->authorized_access_priority);
        tsv->svv[1] = LSB(tp->authorized_access_priority);

	return (0);
}

static int smctr_make_addr_mod(struct net_device *dev, MAC_SUB_VECTOR *tsv)
{
        tsv->svi = ADDRESS_MODIFER;
        tsv->svl = S_ADDRESS_MODIFER;

        tsv->svv[0] = 0;
        tsv->svv[1] = 0;

        return (0);
}

static int smctr_make_auth_funct_class(struct net_device *dev,
        MAC_SUB_VECTOR *tsv)
{
        struct net_local *tp = netdev_priv(dev);

        tsv->svi = AUTHORIZED_FUNCTION_CLASS;
        tsv->svl = S_AUTHORIZED_FUNCTION_CLASS;

        tsv->svv[0] = MSB(tp->authorized_function_classes);
        tsv->svv[1] = LSB(tp->authorized_function_classes);

        return (0);
}

static int smctr_make_corr(struct net_device *dev,
        MAC_SUB_VECTOR *tsv, __u16 correlator)
{
        tsv->svi = CORRELATOR;
        tsv->svl = S_CORRELATOR;

        tsv->svv[0] = MSB(correlator);
        tsv->svv[1] = LSB(correlator);

        return (0);
}

static int smctr_make_funct_addr(struct net_device *dev, MAC_SUB_VECTOR *tsv)
{
        struct net_local *tp = netdev_priv(dev);

        smctr_get_functional_address(dev);

        tsv->svi = FUNCTIONAL_ADDRESS;
        tsv->svl = S_FUNCTIONAL_ADDRESS;

        tsv->svv[0] = MSB(tp->misc_command_data[0]);
        tsv->svv[1] = LSB(tp->misc_command_data[0]);

        tsv->svv[2] = MSB(tp->misc_command_data[1]);
        tsv->svv[3] = LSB(tp->misc_command_data[1]);

        return (0);
}

static int smctr_make_group_addr(struct net_device *dev, MAC_SUB_VECTOR *tsv)
{
        struct net_local *tp = netdev_priv(dev);

        smctr_get_group_address(dev);

        tsv->svi = GROUP_ADDRESS;
        tsv->svl = S_GROUP_ADDRESS;

        tsv->svv[0] = MSB(tp->misc_command_data[0]);
        tsv->svv[1] = LSB(tp->misc_command_data[0]);

        tsv->svv[2] = MSB(tp->misc_command_data[1]);
        tsv->svv[3] = LSB(tp->misc_command_data[1]);

        /* Set Group Address Sub-vector to all zeros if only the
         * Group Address/Functional Address Indicator is set.
         */
        if(tsv->svv[0] == 0x80 && tsv->svv[1] == 0x00 &&
	   tsv->svv[2] == 0x00 && tsv->svv[3] == 0x00)
                tsv->svv[0] = 0x00;

        return (0);
}

static int smctr_make_phy_drop_num(struct net_device *dev,
        MAC_SUB_VECTOR *tsv)
{
        struct net_local *tp = netdev_priv(dev);

        smctr_get_physical_drop_number(dev);

        tsv->svi = PHYSICAL_DROP;
        tsv->svl = S_PHYSICAL_DROP;

        tsv->svv[0] = MSB(tp->misc_command_data[0]);
        tsv->svv[1] = LSB(tp->misc_command_data[0]);

        tsv->svv[2] = MSB(tp->misc_command_data[1]);
        tsv->svv[3] = LSB(tp->misc_command_data[1]);

        return (0);
}

static int smctr_make_product_id(struct net_device *dev, MAC_SUB_VECTOR *tsv)
{
        int i;

        tsv->svi = PRODUCT_INSTANCE_ID;
        tsv->svl = S_PRODUCT_INSTANCE_ID;

        for(i = 0; i < 18; i++)
                tsv->svv[i] = 0xF0;

        return (0);
}

static int smctr_make_station_id(struct net_device *dev, MAC_SUB_VECTOR *tsv)
{
        struct net_local *tp = netdev_priv(dev);

        smctr_get_station_id(dev);

        tsv->svi = STATION_IDENTIFER;
        tsv->svl = S_STATION_IDENTIFER;

        tsv->svv[0] = MSB(tp->misc_command_data[0]);
        tsv->svv[1] = LSB(tp->misc_command_data[0]);

        tsv->svv[2] = MSB(tp->misc_command_data[1]);
        tsv->svv[3] = LSB(tp->misc_command_data[1]);

        tsv->svv[4] = MSB(tp->misc_command_data[2]);
        tsv->svv[5] = LSB(tp->misc_command_data[2]);

        return (0);
}

static int smctr_make_ring_station_status(struct net_device *dev,
        MAC_SUB_VECTOR * tsv)
{
        tsv->svi = RING_STATION_STATUS;
        tsv->svl = S_RING_STATION_STATUS;

        tsv->svv[0] = 0;
        tsv->svv[1] = 0;
        tsv->svv[2] = 0;
        tsv->svv[3] = 0;
        tsv->svv[4] = 0;
        tsv->svv[5] = 0;

        return (0);
}

static int smctr_make_ring_station_version(struct net_device *dev,
        MAC_SUB_VECTOR *tsv)
{
        struct net_local *tp = netdev_priv(dev);

        tsv->svi = RING_STATION_VERSION_NUMBER;
        tsv->svl = S_RING_STATION_VERSION_NUMBER;

        tsv->svv[0] = 0xe2;            /* EBCDIC - S */
        tsv->svv[1] = 0xd4;            /* EBCDIC - M */
        tsv->svv[2] = 0xc3;            /* EBCDIC - C */
        tsv->svv[3] = 0x40;            /* EBCDIC -   */
        tsv->svv[4] = 0xe5;            /* EBCDIC - V */
        tsv->svv[5] = 0xF0 + (tp->microcode_version >> 4);
        tsv->svv[6] = 0xF0 + (tp->microcode_version & 0x0f);
        tsv->svv[7] = 0x40;            /* EBCDIC -   */
        tsv->svv[8] = 0xe7;            /* EBCDIC - X */

        if(tp->extra_info & CHIP_REV_MASK)
                tsv->svv[9] = 0xc5;    /* EBCDIC - E */
        else
                tsv->svv[9] = 0xc4;    /* EBCDIC - D */

        return (0);
}

static int smctr_make_tx_status_code(struct net_device *dev,
        MAC_SUB_VECTOR *tsv, __u16 tx_fstatus)
{
        tsv->svi = TRANSMIT_STATUS_CODE;
        tsv->svl = S_TRANSMIT_STATUS_CODE;

	tsv->svv[0] = ((tx_fstatus & 0x0100 >> 6) | IBM_PASS_SOURCE_ADDR);

        /* Stripped frame status of Transmitted Frame */
        tsv->svv[1] = tx_fstatus & 0xff;

        return (0);
}

static int smctr_make_upstream_neighbor_addr(struct net_device *dev,
        MAC_SUB_VECTOR *tsv)
{
        struct net_local *tp = netdev_priv(dev);

        smctr_get_upstream_neighbor_addr(dev);

        tsv->svi = UPSTREAM_NEIGHBOR_ADDRESS;
        tsv->svl = S_UPSTREAM_NEIGHBOR_ADDRESS;

        tsv->svv[0] = MSB(tp->misc_command_data[0]);
        tsv->svv[1] = LSB(tp->misc_command_data[0]);

        tsv->svv[2] = MSB(tp->misc_command_data[1]);
        tsv->svv[3] = LSB(tp->misc_command_data[1]);

        tsv->svv[4] = MSB(tp->misc_command_data[2]);
        tsv->svv[5] = LSB(tp->misc_command_data[2]);

        return (0);
}

static int smctr_make_wrap_data(struct net_device *dev, MAC_SUB_VECTOR *tsv)
{
        tsv->svi = WRAP_DATA;
        tsv->svl = S_WRAP_DATA;

        return (0);
}

/*
 * Open/initialize the board. This is called sometime after
 * booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int smctr_open(struct net_device *dev)
{
        int err;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_open\n", dev->name);

        err = smctr_init_adapter(dev);
        if(err < 0)
                return (err);

        return (err);
}

/* Interrupt driven open of Token card. */
static int smctr_open_tr(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned long flags;
        int err;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_open_tr\n", dev->name);

        /* Now we can actually open the adapter. */
        if(tp->status == OPEN)
                return (0);
        if(tp->status != INITIALIZED)
                return (-1);

	/* FIXME: it would work a lot better if we masked the irq sources
	   on the card here, then we could skip the locking and poll nicely */
	spin_lock_irqsave(&tp->lock, flags);
	
        smctr_set_page(dev, (__u8 *)tp->ram_access);

        if((err = smctr_issue_resume_rx_fcb_cmd(dev, (short)MAC_QUEUE)))
                goto out;

        if((err = smctr_issue_resume_rx_bdb_cmd(dev, (short)MAC_QUEUE)))
                goto out;

        if((err = smctr_issue_resume_rx_fcb_cmd(dev, (short)NON_MAC_QUEUE)))
                goto out;

        if((err = smctr_issue_resume_rx_bdb_cmd(dev, (short)NON_MAC_QUEUE)))
                goto out;

        tp->status = CLOSED;

        /* Insert into the Ring or Enter Loopback Mode. */
        if((tp->mode_bits & LOOPING_MODE_MASK) == LOOPBACK_MODE_1)
        {
                tp->status = CLOSED;

                if(!(err = smctr_issue_trc_loopback_cmd(dev)))
                {
                        if(!(err = smctr_wait_cmd(dev)))
                                tp->status = OPEN;
                }

                smctr_status_chg(dev);
        }
        else
        {
                if((tp->mode_bits & LOOPING_MODE_MASK) == LOOPBACK_MODE_2)
                {
                        tp->status = CLOSED;
                        if(!(err = smctr_issue_tri_loopback_cmd(dev)))
                        {
                                if(!(err = smctr_wait_cmd(dev)))
                                        tp->status = OPEN;
                        }

                        smctr_status_chg(dev);
                }
                else
                {
                        if((tp->mode_bits & LOOPING_MODE_MASK)
                                == LOOPBACK_MODE_3)
                        {
                                tp->status = CLOSED;
                                if(!(err = smctr_lobe_media_test_cmd(dev)))
                                {
                                        if(!(err = smctr_wait_cmd(dev)))
                                                tp->status = OPEN;
                                }
                                smctr_status_chg(dev);
                        }
                        else
                        {
                                if(!(err = smctr_lobe_media_test(dev)))
                                        err = smctr_issue_insert_cmd(dev);
				else
                                {
                                        if(err == LOBE_MEDIA_TEST_FAILED)
                                                printk(KERN_WARNING "%s: Lobe Media Test Failure - Check cable?\n", dev->name);
                                }
                        }
                }
        }

out:
        spin_unlock_irqrestore(&tp->lock, flags);

        return (err);
}

/* Check for a network adapter of this type, 
 * and return device structure if one exists.
 */
struct net_device __init *smctr_probe(int unit)
{
	struct net_device *dev = alloc_trdev(sizeof(struct net_local));
	static const unsigned ports[] = {
		0x200, 0x220, 0x240, 0x260, 0x280, 0x2A0, 0x2C0, 0x2E0, 0x300,
		0x320, 0x340, 0x360, 0x380, 0
	};
	const unsigned *port;
        int err = 0;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0) {
		sprintf(dev->name, "tr%d", unit);
		netdev_boot_setup_check(dev);
	}

        if (dev->base_addr > 0x1ff)    /* Check a single specified location. */
		err = smctr_probe1(dev, dev->base_addr);
        else if(dev->base_addr != 0)  /* Don't probe at all. */
                err =-ENXIO;
	else {
		for (port = ports; *port; port++) {
			err = smctr_probe1(dev, *port);
			if (!err)
				break;
		}
	}
	if (err)
		goto out;
	err = register_netdev(dev);
	if (err)
		goto out1;
	return dev;
out1:
#ifdef CONFIG_MCA_LEGACY
	{ struct net_local *tp = netdev_priv(dev);
	  if (tp->slot_num)
		mca_mark_as_unused(tp->slot_num);
	}
#endif
	release_region(dev->base_addr, SMCTR_IO_EXTENT);
	free_irq(dev->irq, dev);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

static const struct net_device_ops smctr_netdev_ops = {
	.ndo_open          = smctr_open,
	.ndo_stop          = smctr_close,
	.ndo_start_xmit    = smctr_send_packet,
	.ndo_tx_timeout	   = smctr_timeout,
	.ndo_get_stats     = smctr_get_stats,
	.ndo_set_multicast_list = smctr_set_multicast_list,
};

static int __init smctr_probe1(struct net_device *dev, int ioaddr)
{
        static unsigned version_printed;
        struct net_local *tp = netdev_priv(dev);
        int err;
        __u32 *ram;

        if(smctr_debug && version_printed++ == 0)
                printk(version);

        spin_lock_init(&tp->lock);
        dev->base_addr = ioaddr;

	/* Actually detect an adapter now. */
        err = smctr_chk_isa(dev);
        if(err < 0)
        {
		if ((err = smctr_chk_mca(dev)) < 0) {
			err = -ENODEV;
			goto out;
		}
        }

        tp = netdev_priv(dev);
        dev->mem_start = tp->ram_base;
        dev->mem_end = dev->mem_start + 0x10000;
        ram = (__u32 *)phys_to_virt(dev->mem_start);
        tp->ram_access = *(__u32 *)&ram;
	tp->status = NOT_INITIALIZED;

        err = smctr_load_firmware(dev);
        if(err != UCODE_PRESENT && err != SUCCESS)
        {
                printk(KERN_ERR "%s: Firmware load failed (%d)\n", dev->name, err);
		err = -EIO;
		goto out;
        }

	/* Allow user to specify ring speed on module insert. */
	if(ringspeed == 4)
		tp->media_type = MEDIA_UTP_4;
	else
		tp->media_type = MEDIA_UTP_16;

        printk(KERN_INFO "%s: %s %s at Io %#4x, Irq %d, Rom %#4x, Ram %#4x.\n",
                dev->name, smctr_name, smctr_model,
                (unsigned int)dev->base_addr,
                dev->irq, tp->rom_base, tp->ram_base);

	dev->netdev_ops = &smctr_netdev_ops;
        dev->watchdog_timeo	= HZ;
        return (0);

out:
	return err;
}

static int smctr_process_rx_packet(MAC_HEADER *rmf, __u16 size,
        struct net_device *dev, __u16 rx_status)
{
        struct net_local *tp = netdev_priv(dev);
        struct sk_buff *skb;
        __u16 rcode, correlator;
        int err = 0;
        __u8 xframe = 1;

        rmf->vl = SWAP_BYTES(rmf->vl);
        if(rx_status & FCB_RX_STATUS_DA_MATCHED)
        {
                switch(rmf->vc)
                {
                        /* Received MAC Frames Processed by RS. */
                        case INIT:
                                if((rcode = smctr_rcv_init(dev, rmf, &correlator)) == HARDWARE_FAILED)
                                {
                                        return (rcode);
                                }

                                if((err = smctr_send_rsp(dev, rmf, rcode,
                                        correlator)))
                                {
                                        return (err);
                                }
                                break;

                        case CHG_PARM:
                                if((rcode = smctr_rcv_chg_param(dev, rmf,
                                        &correlator)) ==HARDWARE_FAILED)
                                {
                                        return (rcode);
                                }

                                if((err = smctr_send_rsp(dev, rmf, rcode,
                                        correlator)))
                                {
                                        return (err);
                                }
                                break;

                        case RQ_ADDR:
                                if((rcode = smctr_rcv_rq_addr_state_attch(dev,
                                        rmf, &correlator)) != POSITIVE_ACK)
                                {
                                        if(rcode == HARDWARE_FAILED)
                                                return (rcode);
                                        else
                                                return (smctr_send_rsp(dev, rmf,
                                                        rcode, correlator));
                                }

                                if((err = smctr_send_rpt_addr(dev, rmf,
                                        correlator)))
                                {
                                        return (err);
                                }
                                break;

                        case RQ_ATTCH:
                                if((rcode = smctr_rcv_rq_addr_state_attch(dev,
                                        rmf, &correlator)) != POSITIVE_ACK)
                                {
                                        if(rcode == HARDWARE_FAILED)
                                                return (rcode);
                                        else
                                                return (smctr_send_rsp(dev, rmf,
                                                        rcode,
                                                        correlator));
                                }

                                if((err = smctr_send_rpt_attch(dev, rmf,
                                        correlator)))
                                {
                                        return (err);
                                }
                                break;

                        case RQ_STATE:
                                if((rcode = smctr_rcv_rq_addr_state_attch(dev,
                                        rmf, &correlator)) != POSITIVE_ACK)
                                {
                                        if(rcode == HARDWARE_FAILED)
                                                return (rcode);
                                        else
                                                return (smctr_send_rsp(dev, rmf,
                                                        rcode,
                                                        correlator));
                                }

                                if((err = smctr_send_rpt_state(dev, rmf,
                                        correlator)))
                                {
                                        return (err);
                                }
                                break;

                        case TX_FORWARD: {
        			__u16 uninitialized_var(tx_fstatus);

                                if((rcode = smctr_rcv_tx_forward(dev, rmf))
                                        != POSITIVE_ACK)
                                {
                                        if(rcode == HARDWARE_FAILED)
                                                return (rcode);
                                        else
                                                return (smctr_send_rsp(dev, rmf,
                                                        rcode,
                                                        correlator));
                                }

                                if((err = smctr_send_tx_forward(dev, rmf,
                                        &tx_fstatus)) == HARDWARE_FAILED)
                                {
                                        return (err);
                                }

                                if(err == A_FRAME_WAS_FORWARDED)
                                {
                                        if((err = smctr_send_rpt_tx_forward(dev,
						rmf, tx_fstatus))
                                                == HARDWARE_FAILED)
                                        {
                                                return (err);
                                        }
                                }
                                break;
			}

                        /* Received MAC Frames Processed by CRS/REM/RPS. */
                        case RSP:
                        case RQ_INIT:
                        case RPT_NEW_MON:
                        case RPT_SUA_CHG:
                        case RPT_ACTIVE_ERR:
                        case RPT_NN_INCMP:
                        case RPT_ERROR:
                        case RPT_ATTCH:
                        case RPT_STATE:
                        case RPT_ADDR:
                                break;

                        /* Rcvd Att. MAC Frame (if RXATMAC set) or UNKNOWN */
                        default:
                                xframe = 0;
                                if(!(tp->receive_mask & ACCEPT_ATT_MAC_FRAMES))
                                {
                                        rcode = smctr_rcv_unknown(dev, rmf,
                                                &correlator);
                                        if((err = smctr_send_rsp(dev, rmf,rcode,
                                                correlator)))
                                        {
                                                return (err);
                                        }
                                }

                                break;
                }
        }
        else
        {
                /* 1. DA doesn't match (Promiscuous Mode).
                 * 2. Parse for Extended MAC Frame Type.
                 */
                switch(rmf->vc)
                {
                        case RSP:
                        case INIT:
                        case RQ_INIT:
                        case RQ_ADDR:
                        case RQ_ATTCH:
                        case RQ_STATE:
                        case CHG_PARM:
                        case RPT_ADDR:
                        case RPT_ERROR:
                        case RPT_ATTCH:
                        case RPT_STATE:
                        case RPT_NEW_MON:
                        case RPT_SUA_CHG:
                        case RPT_NN_INCMP:
                        case RPT_ACTIVE_ERR:
                                break;

                        default:
                                xframe = 0;
                                break;
                }
        }

        /* NOTE: UNKNOWN MAC frames will NOT be passed up unless
         * ACCEPT_ATT_MAC_FRAMES is set.
         */
        if(((tp->receive_mask & ACCEPT_ATT_MAC_FRAMES) &&
	    (xframe == (__u8)0)) ||
	   ((tp->receive_mask & ACCEPT_EXT_MAC_FRAMES) &&
	    (xframe == (__u8)1)))
        {
                rmf->vl = SWAP_BYTES(rmf->vl);

                if (!(skb = dev_alloc_skb(size)))
			return -ENOMEM;
                skb->len = size;

                /* Slide data into a sleek skb. */
                skb_put(skb, skb->len);
                skb_copy_to_linear_data(skb, rmf, skb->len);

                /* Update Counters */
                tp->MacStat.rx_packets++;
                tp->MacStat.rx_bytes += skb->len;

                /* Kick the packet on up. */
                skb->protocol = tr_type_trans(skb, dev);
                netif_rx(skb);
                err = 0;
        }

        return (err);
}

/* Adapter RAM test. Incremental word ODD boundary data test. */
static int smctr_ram_memory_test(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        __u16 page, pages_of_ram, start_pattern = 0, word_pattern = 0,
                word_read = 0, err_word = 0, err_pattern = 0;
        unsigned int err_offset;
        __u32 j, pword;
        __u8 err = 0;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_ram_memory_test\n", dev->name);

        start_pattern   = 0x0001;
        pages_of_ram    = tp->ram_size / tp->ram_usable;
        pword           = tp->ram_access;

        /* Incremental word ODD boundary test. */
        for(page = 0; (page < pages_of_ram) && (~err);
                page++, start_pattern += 0x8000)
        {
                smctr_set_page(dev, (__u8 *)(tp->ram_access
                        + (page * tp->ram_usable * 1024) + 1));
                word_pattern = start_pattern;

                for(j = 1; j < (__u32)(tp->ram_usable * 1024) - 1; j += 2)
                        *(__u16 *)(pword + j) = word_pattern++;

                word_pattern = start_pattern;

                for(j = 1; j < (__u32)(tp->ram_usable * 1024) - 1 && (~err);
		    j += 2, word_pattern++)
                {
                        word_read = *(__u16 *)(pword + j);
                        if(word_read != word_pattern)
                        {
                                err             = (__u8)1;
                                err_offset      = j;
                                err_word        = word_read;
                                err_pattern     = word_pattern;
                                return (RAM_TEST_FAILED);
                        }
                }
        }

        /* Zero out memory. */
        for(page = 0; page < pages_of_ram && (~err); page++)
        {
                smctr_set_page(dev, (__u8 *)(tp->ram_access
                        + (page * tp->ram_usable * 1024)));
                word_pattern = 0;

                for(j = 0; j < (__u32)tp->ram_usable * 1024; j +=2)
                        *(__u16 *)(pword + j) = word_pattern;

                for(j =0; j < (__u32)tp->ram_usable * 1024 && (~err); j += 2)
                {
                        word_read = *(__u16 *)(pword + j);
                        if(word_read != word_pattern)
                        {
                                err             = (__u8)1;
                                err_offset      = j;
                                err_word        = word_read;
                                err_pattern     = word_pattern;
                                return (RAM_TEST_FAILED);
                        }
                }
        }

        smctr_set_page(dev, (__u8 *)tp->ram_access);

        return (0);
}

static int smctr_rcv_chg_param(struct net_device *dev, MAC_HEADER *rmf,
        __u16 *correlator)
{
        MAC_SUB_VECTOR *rsv;
        signed short vlen;
        __u16 rcode = POSITIVE_ACK;
        unsigned int svectors = F_NO_SUB_VECTORS_FOUND;

        /* This Frame can only come from a CRS */
        if((rmf->dc_sc & SC_MASK) != SC_CRS)
                return(E_INAPPROPRIATE_SOURCE_CLASS);

        /* Remove MVID Length from total length. */
        vlen = (signed short)rmf->vl - 4;

        /* Point to First SVID */
        rsv = (MAC_SUB_VECTOR *)((__u32)rmf + sizeof(MAC_HEADER));

        /* Search for Appropriate SVID's. */
        while((vlen > 0) && (rcode == POSITIVE_ACK))
        {
                switch(rsv->svi)
                {
                        case CORRELATOR:
                                svectors |= F_CORRELATOR;
                                rcode = smctr_set_corr(dev, rsv, correlator);
                                break;

                        case LOCAL_RING_NUMBER:
                                svectors |= F_LOCAL_RING_NUMBER;
                                rcode = smctr_set_local_ring_num(dev, rsv);
                                break;

                        case ASSIGN_PHYSICAL_DROP:
                                svectors |= F_ASSIGN_PHYSICAL_DROP;
                                rcode = smctr_set_phy_drop(dev, rsv);
                                break;

                        case ERROR_TIMER_VALUE:
                                svectors |= F_ERROR_TIMER_VALUE;
                                rcode = smctr_set_error_timer_value(dev, rsv);
                                break;

                        case AUTHORIZED_FUNCTION_CLASS:
                                svectors |= F_AUTHORIZED_FUNCTION_CLASS;
                                rcode = smctr_set_auth_funct_class(dev, rsv);
                                break;

                        case AUTHORIZED_ACCESS_PRIORITY:
                                svectors |= F_AUTHORIZED_ACCESS_PRIORITY;
                                rcode = smctr_set_auth_access_pri(dev, rsv);
                                break;

                        default:
                                rcode = E_SUB_VECTOR_UNKNOWN;
                                break;
                }

                /* Let Sender Know if SUM of SV length's is
                 * larger then length in MVID length field
                 */
                if((vlen -= rsv->svl) < 0)
                        rcode = E_VECTOR_LENGTH_ERROR;

                rsv = (MAC_SUB_VECTOR *)((__u32)rsv + rsv->svl);
        }

        if(rcode == POSITIVE_ACK)
        {
                /* Let Sender Know if MVID length field
                 * is larger then SUM of SV length's
                 */
                if(vlen != 0)
                        rcode = E_VECTOR_LENGTH_ERROR;
                else
		{
                	/* Let Sender Know if Expected SVID Missing */
                	if((svectors & R_CHG_PARM) ^ R_CHG_PARM)
                        	rcode = E_MISSING_SUB_VECTOR;
		}
        }

        return (rcode);
}

static int smctr_rcv_init(struct net_device *dev, MAC_HEADER *rmf,
        __u16 *correlator)
{
        MAC_SUB_VECTOR *rsv;
        signed short vlen;
        __u16 rcode = POSITIVE_ACK;
        unsigned int svectors = F_NO_SUB_VECTORS_FOUND;

        /* This Frame can only come from a RPS */
        if((rmf->dc_sc & SC_MASK) != SC_RPS)
                return (E_INAPPROPRIATE_SOURCE_CLASS);

        /* Remove MVID Length from total length. */
        vlen = (signed short)rmf->vl - 4;

        /* Point to First SVID */
        rsv = (MAC_SUB_VECTOR *)((__u32)rmf + sizeof(MAC_HEADER));

        /* Search for Appropriate SVID's */
        while((vlen > 0) && (rcode == POSITIVE_ACK))
        {
                switch(rsv->svi)
                {
                        case CORRELATOR:
                                svectors |= F_CORRELATOR;
                                rcode = smctr_set_corr(dev, rsv, correlator);
                                break;

                        case LOCAL_RING_NUMBER:
                                svectors |= F_LOCAL_RING_NUMBER;
                                rcode = smctr_set_local_ring_num(dev, rsv);
                                break;

                        case ASSIGN_PHYSICAL_DROP:
                                svectors |= F_ASSIGN_PHYSICAL_DROP;
                                rcode = smctr_set_phy_drop(dev, rsv);
                                break;

                        case ERROR_TIMER_VALUE:
                                svectors |= F_ERROR_TIMER_VALUE;
                                rcode = smctr_set_error_timer_value(dev, rsv);
                                break;

                        default:
                                rcode = E_SUB_VECTOR_UNKNOWN;
                                break;
                }

                /* Let Sender Know if SUM of SV length's is
                 * larger then length in MVID length field
		 */
                if((vlen -= rsv->svl) < 0)
                        rcode = E_VECTOR_LENGTH_ERROR;

                rsv = (MAC_SUB_VECTOR *)((__u32)rsv + rsv->svl);
        }

        if(rcode == POSITIVE_ACK)
        {
                /* Let Sender Know if MVID length field
                 * is larger then SUM of SV length's
                 */
                if(vlen != 0)
                        rcode = E_VECTOR_LENGTH_ERROR;
                else
		{
                	/* Let Sender Know if Expected SV Missing */
                	if((svectors & R_INIT) ^ R_INIT)
                        	rcode = E_MISSING_SUB_VECTOR;
		}
        }

        return (rcode);
}

static int smctr_rcv_tx_forward(struct net_device *dev, MAC_HEADER *rmf)
{
        MAC_SUB_VECTOR *rsv;
        signed short vlen;
        __u16 rcode = POSITIVE_ACK;
        unsigned int svectors = F_NO_SUB_VECTORS_FOUND;

        /* This Frame can only come from a CRS */
        if((rmf->dc_sc & SC_MASK) != SC_CRS)
                return (E_INAPPROPRIATE_SOURCE_CLASS);

        /* Remove MVID Length from total length */
        vlen = (signed short)rmf->vl - 4;

        /* Point to First SVID */
        rsv = (MAC_SUB_VECTOR *)((__u32)rmf + sizeof(MAC_HEADER));

        /* Search for Appropriate SVID's */
        while((vlen > 0) && (rcode == POSITIVE_ACK))
        {
                switch(rsv->svi)
                {
                        case FRAME_FORWARD:
                                svectors |= F_FRAME_FORWARD;
                                rcode = smctr_set_frame_forward(dev, rsv, 
					rmf->dc_sc);
                                break;

                        default:
                                rcode = E_SUB_VECTOR_UNKNOWN;
                                break;
                }

                /* Let Sender Know if SUM of SV length's is
                 * larger then length in MVID length field
		 */
                if((vlen -= rsv->svl) < 0)
                        rcode = E_VECTOR_LENGTH_ERROR;

                rsv = (MAC_SUB_VECTOR *)((__u32)rsv + rsv->svl);
        }

        if(rcode == POSITIVE_ACK)
        {
                /* Let Sender Know if MVID length field
                 * is larger then SUM of SV length's
                 */
                if(vlen != 0)
                        rcode = E_VECTOR_LENGTH_ERROR;
                else
		{
                	/* Let Sender Know if Expected SV Missing */
                	if((svectors & R_TX_FORWARD) ^ R_TX_FORWARD)
                        	rcode = E_MISSING_SUB_VECTOR;
		}
        }

        return (rcode);
}

static int smctr_rcv_rq_addr_state_attch(struct net_device *dev,
        MAC_HEADER *rmf, __u16 *correlator)
{
        MAC_SUB_VECTOR *rsv;
        signed short vlen;
        __u16 rcode = POSITIVE_ACK;
        unsigned int svectors = F_NO_SUB_VECTORS_FOUND;

        /* Remove MVID Length from total length */
        vlen = (signed short)rmf->vl - 4;

        /* Point to First SVID */
        rsv = (MAC_SUB_VECTOR *)((__u32)rmf + sizeof(MAC_HEADER));

        /* Search for Appropriate SVID's */
        while((vlen > 0) && (rcode == POSITIVE_ACK))
        {
                switch(rsv->svi)
                {
                        case CORRELATOR:
                                svectors |= F_CORRELATOR;
                                rcode = smctr_set_corr(dev, rsv, correlator);
                                break;

                        default:
                                rcode = E_SUB_VECTOR_UNKNOWN;
                                break;
                }

                /* Let Sender Know if SUM of SV length's is
                 * larger then length in MVID length field
                 */
                if((vlen -= rsv->svl) < 0)
                        rcode = E_VECTOR_LENGTH_ERROR;

                rsv = (MAC_SUB_VECTOR *)((__u32)rsv + rsv->svl);
        }

        if(rcode == POSITIVE_ACK)
        {
                /* Let Sender Know if MVID length field
                 * is larger then SUM of SV length's
                 */
                if(vlen != 0)
                        rcode = E_VECTOR_LENGTH_ERROR;
                else
		{
                	/* Let Sender Know if Expected SVID Missing */
                	if((svectors & R_RQ_ATTCH_STATE_ADDR) 
				^ R_RQ_ATTCH_STATE_ADDR)
                        	rcode = E_MISSING_SUB_VECTOR;
			}
        }

        return (rcode);
}

static int smctr_rcv_unknown(struct net_device *dev, MAC_HEADER *rmf,
        __u16 *correlator)
{
        MAC_SUB_VECTOR *rsv;
        signed short vlen;

        *correlator = 0;

        /* Remove MVID Length from total length */
        vlen = (signed short)rmf->vl - 4;

        /* Point to First SVID */
        rsv = (MAC_SUB_VECTOR *)((__u32)rmf + sizeof(MAC_HEADER));

        /* Search for CORRELATOR for RSP to UNKNOWN */
        while((vlen > 0) && (*correlator == 0))
        {
                switch(rsv->svi)
                {
                        case CORRELATOR:
                                smctr_set_corr(dev, rsv, correlator);
                                break;

                        default:
                                break;
                }

                vlen -= rsv->svl;
                rsv = (MAC_SUB_VECTOR *)((__u32)rsv + rsv->svl);
        }

        return (E_UNRECOGNIZED_VECTOR_ID);
}

/*
 * Reset the 825 NIC and exit w:
 * 1. The NIC reset cleared (non-reset state), halted and un-initialized.
 * 2. TINT masked.
 * 3. CBUSY masked.
 * 4. TINT clear.
 * 5. CBUSY clear.
 */
static int smctr_reset_adapter(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int ioaddr = dev->base_addr;

        /* Reseting the NIC will put it in a halted and un-initialized state. */        smctr_set_trc_reset(ioaddr);
        mdelay(200); /* ~2 ms */

        smctr_clear_trc_reset(ioaddr);
        mdelay(200); /* ~2 ms */

        /* Remove any latched interrupts that occurred prior to reseting the
         * adapter or possibily caused by line glitches due to the reset.
         */
        outb(tp->trc_mask | CSR_CLRTINT | CSR_CLRCBUSY, ioaddr + CSR);

        return (0);
}

static int smctr_restart_tx_chain(struct net_device *dev, short queue)
{
        struct net_local *tp = netdev_priv(dev);
        int err = 0;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_restart_tx_chain\n", dev->name);

        if(tp->num_tx_fcbs_used[queue] != 0 &&
	   tp->tx_queue_status[queue] == NOT_TRANSMITING)
        {
                tp->tx_queue_status[queue] = TRANSMITING;
                err = smctr_issue_resume_tx_fcb_cmd(dev, queue);
        }

        return (err);
}

static int smctr_ring_status_chg(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_ring_status_chg\n", dev->name);

        /* Check for ring_status_flag: whenever MONITOR_STATE_BIT
         * Bit is set, check value of monitor_state, only then we
         * enable and start transmit/receive timeout (if and only
         * if it is MS_ACTIVE_MONITOR_STATE or MS_STANDBY_MONITOR_STATE)
         */
        if(tp->ring_status_flags == MONITOR_STATE_CHANGED)
        {
                if((tp->monitor_state == MS_ACTIVE_MONITOR_STATE) ||
		   (tp->monitor_state == MS_STANDBY_MONITOR_STATE))
                {
                        tp->monitor_state_ready = 1;
                }
                else
                {
                        /* if adapter is NOT in either active monitor
                         * or standby monitor state => Disable
                         * transmit/receive timeout.
                         */
                        tp->monitor_state_ready = 0;

			/* Ring speed problem, switching to auto mode. */
			if(tp->monitor_state == MS_MONITOR_FSM_INACTIVE &&
			   !tp->cleanup)
			{
				printk(KERN_INFO "%s: Incorrect ring speed switching.\n",
					dev->name);
				smctr_set_ring_speed(dev);
			}
                }
        }

        if(!(tp->ring_status_flags & RING_STATUS_CHANGED))
                return (0);

        switch(tp->ring_status)
        {
                case RING_RECOVERY:
                        printk(KERN_INFO "%s: Ring Recovery\n", dev->name);
                        break;

                case SINGLE_STATION:
                        printk(KERN_INFO "%s: Single Statinon\n", dev->name);
                        break;

                case COUNTER_OVERFLOW:
                        printk(KERN_INFO "%s: Counter Overflow\n", dev->name);
                        break;

                case REMOVE_RECEIVED:
                        printk(KERN_INFO "%s: Remove Received\n", dev->name);
                        break;

                case AUTO_REMOVAL_ERROR:
                        printk(KERN_INFO "%s: Auto Remove Error\n", dev->name);
                        break;

                case LOBE_WIRE_FAULT:
                        printk(KERN_INFO "%s: Lobe Wire Fault\n", dev->name);
                        break;

                case TRANSMIT_BEACON:
                        printk(KERN_INFO "%s: Transmit Beacon\n", dev->name);
                        break;

                case SOFT_ERROR:
                        printk(KERN_INFO "%s: Soft Error\n", dev->name);
                        break;

                case HARD_ERROR:
                        printk(KERN_INFO "%s: Hard Error\n", dev->name);
                        break;

                case SIGNAL_LOSS:
                        printk(KERN_INFO "%s: Signal Loss\n", dev->name);
                        break;

                default:
			printk(KERN_INFO "%s: Unknown ring status change\n",
				dev->name);
                        break;
        }

        return (0);
}

static int smctr_rx_frame(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        __u16 queue, status, rx_size, err = 0;
        __u8 *pbuff;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_rx_frame\n", dev->name);

        queue = tp->receive_queue_number;

        while((status = tp->rx_fcb_curr[queue]->frame_status) != SUCCESS)
        {
                err = HARDWARE_FAILED;

                if(((status & 0x007f) == 0) ||
		   ((tp->receive_mask & ACCEPT_ERR_PACKETS) != 0))
                {
                        /* frame length less the CRC (4 bytes) + FS (1 byte) */
                        rx_size = tp->rx_fcb_curr[queue]->frame_length - 5;

                        pbuff = smctr_get_rx_pointer(dev, queue);

                        smctr_set_page(dev, pbuff);
                        smctr_disable_16bit(dev);

                        /* pbuff points to addr within one page */
                        pbuff = (__u8 *)PAGE_POINTER(pbuff);

                        if(queue == NON_MAC_QUEUE)
                        {
                                struct sk_buff *skb;

                                skb = dev_alloc_skb(rx_size);
				if (skb) {
                                	skb_put(skb, rx_size);

					skb_copy_to_linear_data(skb, pbuff, rx_size);

                                	/* Update Counters */
                                	tp->MacStat.rx_packets++;
                                	tp->MacStat.rx_bytes += skb->len;

                                	/* Kick the packet on up. */
                                	skb->protocol = tr_type_trans(skb, dev);
                                	netif_rx(skb);
				} else {
				}
                        }
                        else
                                smctr_process_rx_packet((MAC_HEADER *)pbuff,
                                        rx_size, dev, status);
                }

                smctr_enable_16bit(dev);
                smctr_set_page(dev, (__u8 *)tp->ram_access);
                smctr_update_rx_chain(dev, queue);

                if(err != SUCCESS)
                        break;
        }

        return (err);
}

static int smctr_send_dat(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int i, err;
        MAC_HEADER *tmf;
        FCBlock *fcb;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_send_dat\n", dev->name);

        if((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE,
                sizeof(MAC_HEADER))) == (FCBlock *)(-1L))
        {
                return (OUT_OF_RESOURCES);
        }

        /* Initialize DAT Data Fields. */
        tmf = (MAC_HEADER *)fcb->bdb_ptr->data_block_ptr;
        tmf->ac = MSB(AC_FC_DAT);
        tmf->fc = LSB(AC_FC_DAT);

        for(i = 0; i < 6; i++)
        {
                tmf->sa[i] = dev->dev_addr[i];
                tmf->da[i] = dev->dev_addr[i];

        }

        tmf->vc        = DAT;
        tmf->dc_sc     = DC_RS | SC_RS;
        tmf->vl        = 4;
        tmf->vl        = SWAP_BYTES(tmf->vl);

        /* Start Transmit. */
        if((err = smctr_trc_send_packet(dev, fcb, MAC_QUEUE)))
                return (err);

        /* Wait for Transmit to Complete */
        for(i = 0; i < 10000; i++)
        {
                if(fcb->frame_status & FCB_COMMAND_DONE)
                        break;
                mdelay(1);
        }

        /* Check if GOOD frame Tx'ed. */
        if(!(fcb->frame_status &  FCB_COMMAND_DONE) ||
	   fcb->frame_status & (FCB_TX_STATUS_E | FCB_TX_AC_BITS))
        {
                return (INITIALIZE_FAILED);
        }

        /* De-allocated Tx FCB and Frame Buffer
         * The FCB must be de-allocated manually if executing with
         * interrupts disabled, other wise the ISR (LM_Service_Events)
         * will de-allocate it when the interrupt occurs.
         */
        tp->tx_queue_status[MAC_QUEUE] = NOT_TRANSMITING;
        smctr_update_tx_chain(dev, fcb, MAC_QUEUE);

        return (0);
}

static void smctr_timeout(struct net_device *dev)
{
	/*
         * If we get here, some higher level has decided we are broken.
         * There should really be a "kick me" function call instead.
         *
         * Resetting the token ring adapter takes a long time so just
         * fake transmission time and go on trying. Our own timeout
         * routine is in sktr_timer_chk()
         */
        dev->trans_start = jiffies;
        netif_wake_queue(dev);
}

/*
 * Gets skb from system, queues it and checks if it can be sent
 */
static netdev_tx_t smctr_send_packet(struct sk_buff *skb,
					   struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_send_packet\n", dev->name);

        /*
         * Block a transmit overlap
         */
         
        netif_stop_queue(dev);

        if(tp->QueueSkb == 0)
                return NETDEV_TX_BUSY;     /* Return with tbusy set: queue full */

        tp->QueueSkb--;
        skb_queue_tail(&tp->SendSkbQueue, skb);
        smctr_hardware_send_packet(dev, tp);
        if(tp->QueueSkb > 0)
		netif_wake_queue(dev);
		
        return NETDEV_TX_OK;
}

static int smctr_send_lobe_media_test(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
	MAC_SUB_VECTOR *tsv;
	MAC_HEADER *tmf;
        FCBlock *fcb;
	__u32 i;
	int err;

        if(smctr_debug > 15)
                printk(KERN_DEBUG "%s: smctr_send_lobe_media_test\n", dev->name);

        if((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE, sizeof(struct trh_hdr)
                + S_WRAP_DATA + S_WRAP_DATA)) == (FCBlock *)(-1L))
        {
                return (OUT_OF_RESOURCES);
        }

        /* Initialize DAT Data Fields. */
        tmf = (MAC_HEADER *)fcb->bdb_ptr->data_block_ptr;
        tmf->ac = MSB(AC_FC_LOBE_MEDIA_TEST);
        tmf->fc = LSB(AC_FC_LOBE_MEDIA_TEST);

        for(i = 0; i < 6; i++)
        {
                tmf->da[i] = 0;
                tmf->sa[i] = dev->dev_addr[i];
        }

        tmf->vc        = LOBE_MEDIA_TEST;
        tmf->dc_sc     = DC_RS | SC_RS;
        tmf->vl        = 4;

        tsv = (MAC_SUB_VECTOR *)((__u32)tmf + sizeof(MAC_HEADER));
        smctr_make_wrap_data(dev, tsv);
        tmf->vl += tsv->svl;

        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_wrap_data(dev, tsv);
        tmf->vl += tsv->svl;

        /* Start Transmit. */
        tmf->vl = SWAP_BYTES(tmf->vl);
        if((err = smctr_trc_send_packet(dev, fcb, MAC_QUEUE)))
                return (err);

        /* Wait for Transmit to Complete. (10 ms). */
        for(i=0; i < 10000; i++)
        {
                if(fcb->frame_status & FCB_COMMAND_DONE)
                        break;
                mdelay(1);
        }

        /* Check if GOOD frame Tx'ed */
        if(!(fcb->frame_status & FCB_COMMAND_DONE) ||
	   fcb->frame_status & (FCB_TX_STATUS_E | FCB_TX_AC_BITS))
        {
                return (LOBE_MEDIA_TEST_FAILED);
        }

        /* De-allocated Tx FCB and Frame Buffer
         * The FCB must be de-allocated manually if executing with
         * interrupts disabled, other wise the ISR (LM_Service_Events)
         * will de-allocate it when the interrupt occurs.
         */
        tp->tx_queue_status[MAC_QUEUE] = NOT_TRANSMITING;
        smctr_update_tx_chain(dev, fcb, MAC_QUEUE);

        return (0);
}

static int smctr_send_rpt_addr(struct net_device *dev, MAC_HEADER *rmf,
        __u16 correlator)
{
        MAC_HEADER *tmf;
        MAC_SUB_VECTOR *tsv;
        FCBlock *fcb;

        if((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE, sizeof(MAC_HEADER)
		+ S_CORRELATOR + S_PHYSICAL_DROP + S_UPSTREAM_NEIGHBOR_ADDRESS
		+ S_ADDRESS_MODIFER + S_GROUP_ADDRESS + S_FUNCTIONAL_ADDRESS))
		== (FCBlock *)(-1L))
        {
                return (0);
        }

        tmf 		= (MAC_HEADER *)fcb->bdb_ptr->data_block_ptr;
        tmf->vc    	= RPT_ADDR;
        tmf->dc_sc 	= (rmf->dc_sc & SC_MASK) << 4;
        tmf->vl    	= 4;

        smctr_make_8025_hdr(dev, rmf, tmf, AC_FC_RPT_ADDR);

        tsv = (MAC_SUB_VECTOR *)((__u32)tmf + sizeof(MAC_HEADER));
        smctr_make_corr(dev, tsv, correlator);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_phy_drop_num(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_upstream_neighbor_addr(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_addr_mod(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_group_addr(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_funct_addr(dev, tsv);

        tmf->vl += tsv->svl;

        /* Subtract out MVID and MVL which is
         * include in both vl and MAC_HEADER
         */
/*      fcb->frame_length           = tmf->vl + sizeof(MAC_HEADER) - 4;
        fcb->bdb_ptr->buffer_length = tmf->vl + sizeof(MAC_HEADER) - 4;
*/
        tmf->vl = SWAP_BYTES(tmf->vl);

        return (smctr_trc_send_packet(dev, fcb, MAC_QUEUE));
}

static int smctr_send_rpt_attch(struct net_device *dev, MAC_HEADER *rmf,
        __u16 correlator)
{
        MAC_HEADER *tmf;
        MAC_SUB_VECTOR *tsv;
        FCBlock *fcb;

        if((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE, sizeof(MAC_HEADER)
		+ S_CORRELATOR + S_PRODUCT_INSTANCE_ID + S_FUNCTIONAL_ADDRESS
		+ S_AUTHORIZED_FUNCTION_CLASS + S_AUTHORIZED_ACCESS_PRIORITY))
		== (FCBlock *)(-1L))
        {
                return (0);
        }

        tmf 	   = (MAC_HEADER *)fcb->bdb_ptr->data_block_ptr;
        tmf->vc    = RPT_ATTCH;
        tmf->dc_sc = (rmf->dc_sc & SC_MASK) << 4;
        tmf->vl    = 4;

        smctr_make_8025_hdr(dev, rmf, tmf, AC_FC_RPT_ATTCH);

        tsv = (MAC_SUB_VECTOR *)((__u32)tmf + sizeof(MAC_HEADER));
        smctr_make_corr(dev, tsv, correlator);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_product_id(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_funct_addr(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_auth_funct_class(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_access_pri(dev, tsv);

        tmf->vl += tsv->svl;

        /* Subtract out MVID and MVL which is
         * include in both vl and MAC_HEADER
         */
/*      fcb->frame_length           = tmf->vl + sizeof(MAC_HEADER) - 4;
        fcb->bdb_ptr->buffer_length = tmf->vl + sizeof(MAC_HEADER) - 4;
*/
        tmf->vl = SWAP_BYTES(tmf->vl);

        return (smctr_trc_send_packet(dev, fcb, MAC_QUEUE));
}

static int smctr_send_rpt_state(struct net_device *dev, MAC_HEADER *rmf,
        __u16 correlator)
{
        MAC_HEADER *tmf;
        MAC_SUB_VECTOR *tsv;
        FCBlock *fcb;

        if((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE, sizeof(MAC_HEADER)
		+ S_CORRELATOR + S_RING_STATION_VERSION_NUMBER
		+ S_RING_STATION_STATUS + S_STATION_IDENTIFER))
		== (FCBlock *)(-1L))
        {
                return (0);
        }

        tmf 	   = (MAC_HEADER *)fcb->bdb_ptr->data_block_ptr;
        tmf->vc    = RPT_STATE;
        tmf->dc_sc = (rmf->dc_sc & SC_MASK) << 4;
        tmf->vl    = 4;

        smctr_make_8025_hdr(dev, rmf, tmf, AC_FC_RPT_STATE);

        tsv = (MAC_SUB_VECTOR *)((__u32)tmf + sizeof(MAC_HEADER));
        smctr_make_corr(dev, tsv, correlator);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_ring_station_version(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_ring_station_status(dev, tsv);

        tmf->vl += tsv->svl;
        tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
        smctr_make_station_id(dev, tsv);

        tmf->vl += tsv->svl;

        /* Subtract out MVID and MVL which is
         * include in both vl and MAC_HEADER
         */
/*      fcb->frame_length           = tmf->vl + sizeof(MAC_HEADER) - 4;
        fcb->bdb_ptr->buffer_length = tmf->vl + sizeof(MAC_HEADER) - 4;
*/
        tmf->vl = SWAP_BYTES(tmf->vl);

        return (smctr_trc_send_packet(dev, fcb, MAC_QUEUE));
}

static int smctr_send_rpt_tx_forward(struct net_device *dev,
        MAC_HEADER *rmf, __u16 tx_fstatus)
{
        MAC_HEADER *tmf;
        MAC_SUB_VECTOR *tsv;
        FCBlock *fcb;

        if((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE, sizeof(MAC_HEADER)
		+ S_TRANSMIT_STATUS_CODE)) == (FCBlock *)(-1L))
        {
                return (0);
        }

        tmf 	   = (MAC_HEADER *)fcb->bdb_ptr->data_block_ptr;
        tmf->vc    = RPT_TX_FORWARD;
        tmf->dc_sc = (rmf->dc_sc & SC_MASK) << 4;
        tmf->vl    = 4;

        smctr_make_8025_hdr(dev, rmf, tmf, AC_FC_RPT_TX_FORWARD);

        tsv = (MAC_SUB_VECTOR *)((__u32)tmf + sizeof(MAC_HEADER));
        smctr_make_tx_status_code(dev, tsv, tx_fstatus);

        tmf->vl += tsv->svl;

        /* Subtract out MVID and MVL which is
         * include in both vl and MAC_HEADER
         */
/*      fcb->frame_length           = tmf->vl + sizeof(MAC_HEADER) - 4;
        fcb->bdb_ptr->buffer_length = tmf->vl + sizeof(MAC_HEADER) - 4;
*/
        tmf->vl = SWAP_BYTES(tmf->vl);

        return(smctr_trc_send_packet(dev, fcb, MAC_QUEUE));
}

static int smctr_send_rsp(struct net_device *dev, MAC_HEADER *rmf,
        __u16 rcode, __u16 correlator)
{
        MAC_HEADER *tmf;
        MAC_SUB_VECTOR *tsv;
        FCBlock *fcb;

        if((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE, sizeof(MAC_HEADER)
		+ S_CORRELATOR + S_RESPONSE_CODE)) == (FCBlock *)(-1L))
        {
                return (0);
        }

        tmf 	   = (MAC_HEADER *)fcb->bdb_ptr->data_block_ptr;
        tmf->vc    = RSP;
        tmf->dc_sc = (rmf->dc_sc & SC_MASK) << 4;
        tmf->vl    = 4;

        smctr_make_8025_hdr(dev, rmf, tmf, AC_FC_RSP);

        tsv = (MAC_SUB_VECTOR *)((__u32)tmf + sizeof(MAC_HEADER));
        smctr_make_corr(dev, tsv, correlator);

        return (0);
}

static int smctr_send_rq_init(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        MAC_HEADER *tmf;
        MAC_SUB_VECTOR *tsv;
        FCBlock *fcb;
	unsigned int i, count = 0;
	__u16 fstatus;
	int err;

        do {
        	if(((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE, sizeof(MAC_HEADER)
			+ S_PRODUCT_INSTANCE_ID + S_UPSTREAM_NEIGHBOR_ADDRESS
			+ S_RING_STATION_VERSION_NUMBER + S_ADDRESS_MODIFER))
			== (FCBlock *)(-1L)))
                {
                        return (0);
                }

                tmf 	   = (MAC_HEADER *)fcb->bdb_ptr->data_block_ptr;
                tmf->vc    = RQ_INIT;
                tmf->dc_sc = DC_RPS | SC_RS;
                tmf->vl    = 4;

                smctr_make_8025_hdr(dev, NULL, tmf, AC_FC_RQ_INIT);

                tsv = (MAC_SUB_VECTOR *)((__u32)tmf + sizeof(MAC_HEADER));
                smctr_make_product_id(dev, tsv);

                tmf->vl += tsv->svl;
                tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
                smctr_make_upstream_neighbor_addr(dev, tsv);

                tmf->vl += tsv->svl;
                tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
                smctr_make_ring_station_version(dev, tsv);

                tmf->vl += tsv->svl;
                tsv = (MAC_SUB_VECTOR *)((__u32)tsv + tsv->svl);
                smctr_make_addr_mod(dev, tsv);

                tmf->vl += tsv->svl;

                /* Subtract out MVID and MVL which is
                 * include in both vl and MAC_HEADER
                 */
/*              fcb->frame_length           = tmf->vl + sizeof(MAC_HEADER) - 4;
                fcb->bdb_ptr->buffer_length = tmf->vl + sizeof(MAC_HEADER) - 4;
*/
                tmf->vl = SWAP_BYTES(tmf->vl);

                if((err = smctr_trc_send_packet(dev, fcb, MAC_QUEUE)))
                        return (err);

                /* Wait for Transmit to Complete */
      		for(i = 0; i < 10000; i++) 
		{
          		if(fcb->frame_status & FCB_COMMAND_DONE)
              			break;
          		mdelay(1);
      		}

                /* Check if GOOD frame Tx'ed */
                fstatus = fcb->frame_status;

                if(!(fstatus & FCB_COMMAND_DONE))
                        return (HARDWARE_FAILED);

                if(!(fstatus & FCB_TX_STATUS_E))
                        count++;

                /* De-allocated Tx FCB and Frame Buffer
                 * The FCB must be de-allocated manually if executing with
                 * interrupts disabled, other wise the ISR (LM_Service_Events)
                 * will de-allocate it when the interrupt occurs.
                 */
                tp->tx_queue_status[MAC_QUEUE] = NOT_TRANSMITING;
                smctr_update_tx_chain(dev, fcb, MAC_QUEUE);
        } while(count < 4 && ((fstatus & FCB_TX_AC_BITS) ^ FCB_TX_AC_BITS));

	return (smctr_join_complete_state(dev));
}

static int smctr_send_tx_forward(struct net_device *dev, MAC_HEADER *rmf,
        __u16 *tx_fstatus)
{
        struct net_local *tp = netdev_priv(dev);
        FCBlock *fcb;
        unsigned int i;
	int err;

        /* Check if this is the END POINT of the Transmit Forward Chain. */
        if(rmf->vl <= 18)
                return (0);

        /* Allocate Transmit FCB only by requesting 0 bytes
         * of data buffer.
         */
        if((fcb = smctr_get_tx_fcb(dev, MAC_QUEUE, 0)) == (FCBlock *)(-1L))
                return (0);

        /* Set pointer to Transmit Frame Buffer to the data
         * portion of the received TX Forward frame, making
         * sure to skip over the Vector Code (vc) and Vector
         * length (vl).
         */
        fcb->bdb_ptr->trc_data_block_ptr = TRC_POINTER((__u32)rmf 
		+ sizeof(MAC_HEADER) + 2);
        fcb->bdb_ptr->data_block_ptr     = (__u16 *)((__u32)rmf 
		+ sizeof(MAC_HEADER) + 2);

        fcb->frame_length                = rmf->vl - 4 - 2;
        fcb->bdb_ptr->buffer_length      = rmf->vl - 4 - 2;

        if((err = smctr_trc_send_packet(dev, fcb, MAC_QUEUE)))
                return (err);

        /* Wait for Transmit to Complete */
   	for(i = 0; i < 10000; i++) 
	{
       		if(fcb->frame_status & FCB_COMMAND_DONE)
           		break;
        	mdelay(1);
   	}

        /* Check if GOOD frame Tx'ed */
        if(!(fcb->frame_status & FCB_COMMAND_DONE))
        {
                if((err = smctr_issue_resume_tx_fcb_cmd(dev, MAC_QUEUE)))
                        return (err);

      		for(i = 0; i < 10000; i++) 
		{
          		if(fcb->frame_status & FCB_COMMAND_DONE)
              			break;
        		mdelay(1);
      		}

                if(!(fcb->frame_status & FCB_COMMAND_DONE))
                        return (HARDWARE_FAILED);
        }

        *tx_fstatus = fcb->frame_status;

        return (A_FRAME_WAS_FORWARDED);
}

static int smctr_set_auth_access_pri(struct net_device *dev,
        MAC_SUB_VECTOR *rsv)
{
        struct net_local *tp = netdev_priv(dev);

        if(rsv->svl != S_AUTHORIZED_ACCESS_PRIORITY)
                return (E_SUB_VECTOR_LENGTH_ERROR);

        tp->authorized_access_priority = (rsv->svv[0] << 8 | rsv->svv[1]);

        return (POSITIVE_ACK);
}

static int smctr_set_auth_funct_class(struct net_device *dev,
        MAC_SUB_VECTOR *rsv)
{
        struct net_local *tp = netdev_priv(dev);

        if(rsv->svl != S_AUTHORIZED_FUNCTION_CLASS)
                return (E_SUB_VECTOR_LENGTH_ERROR);

        tp->authorized_function_classes = (rsv->svv[0] << 8 | rsv->svv[1]);

        return (POSITIVE_ACK);
}

static int smctr_set_corr(struct net_device *dev, MAC_SUB_VECTOR *rsv,
        __u16 *correlator)
{
        if(rsv->svl != S_CORRELATOR)
                return (E_SUB_VECTOR_LENGTH_ERROR);

        *correlator = (rsv->svv[0] << 8 | rsv->svv[1]);

        return (POSITIVE_ACK);
}

static int smctr_set_error_timer_value(struct net_device *dev,
        MAC_SUB_VECTOR *rsv)
{
	__u16 err_tval;
	int err;

        if(rsv->svl != S_ERROR_TIMER_VALUE)
                return (E_SUB_VECTOR_LENGTH_ERROR);

        err_tval = (rsv->svv[0] << 8 | rsv->svv[1])*10;

        smctr_issue_write_word_cmd(dev, RW_TER_THRESHOLD, &err_tval);

        if((err = smctr_wait_cmd(dev)))
                return (err);

        return (POSITIVE_ACK);
}

static int smctr_set_frame_forward(struct net_device *dev,
        MAC_SUB_VECTOR *rsv, __u8 dc_sc)
{
        if((rsv->svl < 2) || (rsv->svl > S_FRAME_FORWARD))
                return (E_SUB_VECTOR_LENGTH_ERROR);

        if((dc_sc & DC_MASK) != DC_CRS)
        {
                if(rsv->svl >= 2 && rsv->svl < 20)
                	return (E_TRANSMIT_FORWARD_INVALID);

                if((rsv->svv[0] != 0) || (rsv->svv[1] != 0))
                        return (E_TRANSMIT_FORWARD_INVALID);
        }

        return (POSITIVE_ACK);
}

static int smctr_set_local_ring_num(struct net_device *dev,
        MAC_SUB_VECTOR *rsv)
{
        struct net_local *tp = netdev_priv(dev);

        if(rsv->svl != S_LOCAL_RING_NUMBER)
                return (E_SUB_VECTOR_LENGTH_ERROR);

        if(tp->ptr_local_ring_num)
                *(__u16 *)(tp->ptr_local_ring_num) 
			= (rsv->svv[0] << 8 | rsv->svv[1]);

        return (POSITIVE_ACK);
}

static unsigned short smctr_set_ctrl_attention(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        int ioaddr = dev->base_addr;

        if(tp->bic_type == BIC_585_CHIP)
                outb((tp->trc_mask | HWR_CA), ioaddr + HWR);
        else
        {
                outb((tp->trc_mask | CSR_CA), ioaddr + CSR);
                outb(tp->trc_mask, ioaddr + CSR);
        }

        return (0);
}

static void smctr_set_multicast_list(struct net_device *dev)
{
        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_set_multicast_list\n", dev->name);

        return;
}

static int smctr_set_page(struct net_device *dev, __u8 *buf)
{
        struct net_local *tp = netdev_priv(dev);
        __u8 amask;
        __u32 tptr;

        tptr = (__u32)buf - (__u32)tp->ram_access;
        amask = (__u8)((tptr & PR_PAGE_MASK) >> 8);
        outb(amask, dev->base_addr + PR);

        return (0);
}

static int smctr_set_phy_drop(struct net_device *dev, MAC_SUB_VECTOR *rsv)
{
	int err;

        if(rsv->svl != S_PHYSICAL_DROP)
                return (E_SUB_VECTOR_LENGTH_ERROR);

        smctr_issue_write_byte_cmd(dev, RW_PHYSICAL_DROP_NUMBER, &rsv->svv[0]);
        if((err = smctr_wait_cmd(dev)))
                return (err);

        return (POSITIVE_ACK);
}

/* Reset the ring speed to the opposite of what it was. This auto-pilot
 * mode requires a complete reset and re-init of the adapter.
 */
static int smctr_set_ring_speed(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
	int err;

        if(tp->media_type == MEDIA_UTP_16)
                tp->media_type = MEDIA_UTP_4;
        else
                tp->media_type = MEDIA_UTP_16;

        smctr_enable_16bit(dev);

        /* Re-Initialize adapter's internal registers */
        smctr_reset_adapter(dev);

        if((err = smctr_init_card_real(dev)))
                return (err);

        smctr_enable_bic_int(dev);

        if((err = smctr_issue_enable_int_cmd(dev, TRC_INTERRUPT_ENABLE_MASK)))
                return (err);

        smctr_disable_16bit(dev);

	return (0);
}

static int smctr_set_rx_look_ahead(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        __u16 sword, rword;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_set_rx_look_ahead_flag\n", dev->name);

        tp->adapter_flags &= ~(FORCED_16BIT_MODE);
        tp->adapter_flags |= RX_VALID_LOOKAHEAD;

        if(tp->adapter_bus == BUS_ISA16_TYPE)
        {
                sword = *((__u16 *)(tp->ram_access));
                *((__u16 *)(tp->ram_access)) = 0x1234;

                smctr_disable_16bit(dev);
                rword = *((__u16 *)(tp->ram_access));
                smctr_enable_16bit(dev);

                if(rword != 0x1234)
                        tp->adapter_flags |= FORCED_16BIT_MODE;

                *((__u16 *)(tp->ram_access)) = sword;
        }

        return (0);
}

static int smctr_set_trc_reset(int ioaddr)
{
        __u8 r;

        r = inb(ioaddr + MSR);
        outb(MSR_RST | r, ioaddr + MSR);

        return (0);
}

/*
 * This function can be called if the adapter is busy or not.
 */
static int smctr_setup_single_cmd(struct net_device *dev,
        __u16 command, __u16 subcommand)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int err;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_setup_single_cmd\n", dev->name);

        if((err = smctr_wait_while_cbusy(dev)))
                return (err);

        if((err = (unsigned int)smctr_wait_cmd(dev)))
                return (err);

        tp->acb_head->cmd_done_status   = 0;
        tp->acb_head->cmd               = command;
        tp->acb_head->subcmd            = subcommand;

        err = smctr_issue_resume_acb_cmd(dev);

        return (err);
}

/*
 * This function can not be called with the adapter busy.
 */
static int smctr_setup_single_cmd_w_data(struct net_device *dev,
        __u16 command, __u16 subcommand)
{
        struct net_local *tp = netdev_priv(dev);

        tp->acb_head->cmd_done_status   = ACB_COMMAND_NOT_DONE;
        tp->acb_head->cmd               = command;
        tp->acb_head->subcmd            = subcommand;
        tp->acb_head->data_offset_lo
                = (__u16)TRC_POINTER(tp->misc_command_data);

        return(smctr_issue_resume_acb_cmd(dev));
}

static char *smctr_malloc(struct net_device *dev, __u16 size)
{
        struct net_local *tp = netdev_priv(dev);
        char *m;

        m = (char *)(tp->ram_access + tp->sh_mem_used);
        tp->sh_mem_used += (__u32)size;

        return (m);
}

static int smctr_status_chg(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_status_chg\n", dev->name);

        switch(tp->status)
        {
                case OPEN:
                        break;

                case CLOSED:
                        break;

                /* Interrupt driven open() completion. XXX */
                case INITIALIZED:
                        tp->group_address_0 = 0;
                        tp->group_address[0] = 0;
                        tp->group_address[1] = 0;
                        tp->functional_address_0 = 0;
                        tp->functional_address[0] = 0;
                        tp->functional_address[1] = 0;
                        smctr_open_tr(dev);
                        break;

                default:
                        printk(KERN_INFO "%s: status change unknown %x\n",
                                dev->name, tp->status);
                        break;
        }

        return (0);
}

static int smctr_trc_send_packet(struct net_device *dev, FCBlock *fcb,
        __u16 queue)
{
        struct net_local *tp = netdev_priv(dev);
        int err = 0;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_trc_send_packet\n", dev->name);

        fcb->info = FCB_CHAIN_END | FCB_ENABLE_TFS;
        if(tp->num_tx_fcbs[queue] != 1)
                fcb->back_ptr->info = FCB_INTERRUPT_ENABLE | FCB_ENABLE_TFS;

        if(tp->tx_queue_status[queue] == NOT_TRANSMITING)
        {
                tp->tx_queue_status[queue] = TRANSMITING;
                err = smctr_issue_resume_tx_fcb_cmd(dev, queue);
        }

        return (err);
}

static __u16 smctr_tx_complete(struct net_device *dev, __u16 queue)
{
        struct net_local *tp = netdev_priv(dev);
        __u16 status, err = 0;
        int cstatus;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_tx_complete\n", dev->name);

        while((status = tp->tx_fcb_end[queue]->frame_status) != SUCCESS)
        {
                if(status & 0x7e00 )
                {
                        err = HARDWARE_FAILED;
                        break;
                }

                if((err = smctr_update_tx_chain(dev, tp->tx_fcb_end[queue],
                        queue)) != SUCCESS)
                        break;

                smctr_disable_16bit(dev);

                if(tp->mode_bits & UMAC)
                {
                        if(!(status & (FCB_TX_STATUS_AR1 | FCB_TX_STATUS_AR2)))
                                cstatus = NO_SUCH_DESTINATION;
                        else
                        {
                                if(!(status & (FCB_TX_STATUS_CR1 | FCB_TX_STATUS_CR2)))
                                        cstatus = DEST_OUT_OF_RESOURCES;
                                else
                                {
                                        if(status & FCB_TX_STATUS_E)
                                                cstatus = MAX_COLLISIONS;
                                        else
                                                cstatus = SUCCESS;
                                }
                        }
                }
                else
                        cstatus = SUCCESS;

                if(queue == BUG_QUEUE)
                        err = SUCCESS;

                smctr_enable_16bit(dev);
                if(err != SUCCESS)
                        break;
        }

        return (err);
}

static unsigned short smctr_tx_move_frame(struct net_device *dev,
        struct sk_buff *skb, __u8 *pbuff, unsigned int bytes)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int ram_usable;
        __u32 flen, len, offset = 0;
        __u8 *frag, *page;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_tx_move_frame\n", dev->name);

        ram_usable = ((unsigned int)tp->ram_usable) << 10;
        frag       = skb->data;
        flen       = skb->len;

        while(flen > 0 && bytes > 0)
        {
                smctr_set_page(dev, pbuff);

                offset = SMC_PAGE_OFFSET(pbuff);

                if(offset + flen > ram_usable)
                        len = ram_usable - offset;
                else
                        len = flen;

                if(len > bytes)
                        len = bytes;

                page = (char *) (offset + tp->ram_access);
                memcpy(page, frag, len);

                flen -=len;
                bytes -= len;
                frag += len;
                pbuff += len;
        }

        return (0);
}

/* Update the error statistic counters for this adapter. */
static int smctr_update_err_stats(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        struct tr_statistics *tstat = &tp->MacStat;

        if(tstat->internal_errors)
                tstat->internal_errors
                        += *(tp->misc_command_data + 0) & 0x00ff;

        if(tstat->line_errors)
                tstat->line_errors += *(tp->misc_command_data + 0) >> 8;

        if(tstat->A_C_errors)
                tstat->A_C_errors += *(tp->misc_command_data + 1) & 0x00ff;

        if(tstat->burst_errors)
                tstat->burst_errors += *(tp->misc_command_data + 1) >> 8;

        if(tstat->abort_delimiters)
                tstat->abort_delimiters += *(tp->misc_command_data + 2) >> 8;

        if(tstat->recv_congest_count)
                tstat->recv_congest_count
                        += *(tp->misc_command_data + 3) & 0x00ff;

        if(tstat->lost_frames)
                tstat->lost_frames
                        += *(tp->misc_command_data + 3) >> 8;

        if(tstat->frequency_errors)
                tstat->frequency_errors += *(tp->misc_command_data + 4) & 0x00ff;

        if(tstat->frame_copied_errors)
                 tstat->frame_copied_errors
                        += *(tp->misc_command_data + 4) >> 8;

        if(tstat->token_errors)
                tstat->token_errors += *(tp->misc_command_data + 5) >> 8;

        return (0);
}

static int smctr_update_rx_chain(struct net_device *dev, __u16 queue)
{
        struct net_local *tp = netdev_priv(dev);
        FCBlock *fcb;
        BDBlock *bdb;
        __u16 size, len;

        fcb = tp->rx_fcb_curr[queue];
        len = fcb->frame_length;

        fcb->frame_status = 0;
        fcb->info = FCB_CHAIN_END;
        fcb->back_ptr->info = FCB_WARNING;

        tp->rx_fcb_curr[queue] = tp->rx_fcb_curr[queue]->next_ptr;

        /* update RX BDBs */
        size = (len >> RX_BDB_SIZE_SHIFT);
        if(len & RX_DATA_BUFFER_SIZE_MASK)
                size += sizeof(BDBlock);
        size &= (~RX_BDB_SIZE_MASK);

        /* check if wrap around */
        bdb = (BDBlock *)((__u32)(tp->rx_bdb_curr[queue]) + (__u32)(size));
        if((__u32)bdb >= (__u32)tp->rx_bdb_end[queue])
        {
                bdb = (BDBlock *)((__u32)(tp->rx_bdb_head[queue])
                        + (__u32)(bdb) - (__u32)(tp->rx_bdb_end[queue]));
        }

        bdb->back_ptr->info = BDB_CHAIN_END;
        tp->rx_bdb_curr[queue]->back_ptr->info = BDB_NOT_CHAIN_END;
        tp->rx_bdb_curr[queue] = bdb;

        return (0);
}

static int smctr_update_tx_chain(struct net_device *dev, FCBlock *fcb,
        __u16 queue)
{
        struct net_local *tp = netdev_priv(dev);

        if(smctr_debug > 20)
                printk(KERN_DEBUG "smctr_update_tx_chain\n");

        if(tp->num_tx_fcbs_used[queue] <= 0)
                return (HARDWARE_FAILED);
        else
        {
                if(tp->tx_buff_used[queue] < fcb->memory_alloc)
                {
                        tp->tx_buff_used[queue] = 0;
                        return (HARDWARE_FAILED);
                }

                tp->tx_buff_used[queue] -= fcb->memory_alloc;

                /* if all transmit buffer are cleared
                 * need to set the tx_buff_curr[] to tx_buff_head[]
                 * otherwise, tx buffer will be segregate and cannot
                 * accommodate and buffer greater than (curr - head) and
                 * (end - curr) since we do not allow wrap around allocation.
                 */
                if(tp->tx_buff_used[queue] == 0)
                        tp->tx_buff_curr[queue] = tp->tx_buff_head[queue];

                tp->num_tx_fcbs_used[queue]--;
                fcb->frame_status = 0;
                tp->tx_fcb_end[queue] = fcb->next_ptr;
		netif_wake_queue(dev);
                return (0);
        }
}

static int smctr_wait_cmd(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int loop_count = 0x20000;

        if(smctr_debug > 10)
                printk(KERN_DEBUG "%s: smctr_wait_cmd\n", dev->name);

        while(loop_count)
        {
                if(tp->acb_head->cmd_done_status & ACB_COMMAND_DONE)
                        break;
		udelay(1);
                loop_count--;
        }

        if(loop_count == 0)
                return(HARDWARE_FAILED);

        if(tp->acb_head->cmd_done_status & 0xff)
                return(HARDWARE_FAILED);

        return (0);
}

static int smctr_wait_while_cbusy(struct net_device *dev)
{
        struct net_local *tp = netdev_priv(dev);
        unsigned int timeout = 0x20000;
        int ioaddr = dev->base_addr;
        __u8 r;

        if(tp->bic_type == BIC_585_CHIP)
        {
                while(timeout)
                {
                        r = inb(ioaddr + HWR);
                        if((r & HWR_CBUSY) == 0)
                                break;
                        timeout--;
                }
        }
        else
        {
                while(timeout)
                {
                        r = inb(ioaddr + CSR);
                        if((r & CSR_CBUSY) == 0)
                                break;
                        timeout--;
                }
        }

        if(timeout)
                return (0);
        else
                return (HARDWARE_FAILED);
}

#ifdef MODULE

static struct net_device* dev_smctr[SMCTR_MAX_ADAPTERS];
static int io[SMCTR_MAX_ADAPTERS];
static int irq[SMCTR_MAX_ADAPTERS];

MODULE_LICENSE("GPL");
MODULE_FIRMWARE("tr_smctr.bin");

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param(ringspeed, int, 0);

static struct net_device * __init setup_card(int n)
{
	struct net_device *dev = alloc_trdev(sizeof(struct net_local));
	int err;
	
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->irq = irq[n];
	err = smctr_probe1(dev, io[n]);
	if (err) 
		goto out;
		
	err = register_netdev(dev);
	if (err)
		goto out1;
	return dev;
 out1:
#ifdef CONFIG_MCA_LEGACY
	{ struct net_local *tp = netdev_priv(dev);
	  if (tp->slot_num)
		mca_mark_as_unused(tp->slot_num);
	}
#endif
	release_region(dev->base_addr, SMCTR_IO_EXTENT);
	free_irq(dev->irq, dev);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

int __init init_module(void)
{
        int i, found = 0;
	struct net_device *dev;

        for(i = 0; i < SMCTR_MAX_ADAPTERS; i++) {
		dev = io[0]? setup_card(i) : smctr_probe(-1);
		if (!IS_ERR(dev)) {
			++found;
			dev_smctr[i] = dev;
		}
        }

        return found ? 0 : -ENODEV;
}

void __exit cleanup_module(void)
{
        int i;

        for(i = 0; i < SMCTR_MAX_ADAPTERS; i++) {
		struct net_device *dev = dev_smctr[i];

		if (dev) {

			unregister_netdev(dev);
#ifdef CONFIG_MCA_LEGACY
			{ struct net_local *tp = netdev_priv(dev);
			if (tp->slot_num)
				mca_mark_as_unused(tp->slot_num);
			}
#endif
			release_region(dev->base_addr, SMCTR_IO_EXTENT);
			if (dev->irq)
				free_irq(dev->irq, dev);

			free_netdev(dev);
		}
        }
}
#endif /* MODULE */
