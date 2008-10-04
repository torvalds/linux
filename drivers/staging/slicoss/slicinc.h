/**************************************************************************
 *
 * Copyright (c) 2000-2002 Alacritech, Inc.  All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: slicinc.h
 *
 * This file contains all other include files and prototype definitions
 * for the SLICOSS driver.
 */
#ifndef _SLIC_INCLUDE_H_
#define _SLIC_INCLUDE_H_

#include "slic_os.h"
#include "slicdbg.h"
#include "slichw.h"
#include "slic.h"

static int slic_entry_probe(struct pci_dev              *pcidev,
			const struct pci_device_id  *ent);
static void slic_entry_remove(struct pci_dev *pcidev);

static void slic_init_driver(void);
static int  slic_entry_open(struct net_device *dev);
static int  slic_entry_halt(struct net_device *dev);
static int  slic_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int  slic_xmit_start(struct sk_buff *skb, struct net_device *dev);
static void slic_xmit_fail(struct adapter    *adapter,
			struct sk_buff   *skb,
			void *cmd,
			u32           skbtype,
			u32           status);
static void slic_xmit_timeout(struct net_device *dev);
static void slic_config_pci(struct pci_dev *pcidev);
static struct sk_buff *slic_rcvqueue_getnext(struct adapter *adapter);

static inline void slic_reg32_write(void __iomem *reg, u32 value, uint flush);
static inline void slic_reg64_write(struct adapter *adapter, void __iomem *reg,
	u32 value, void __iomem *regh, u32 paddrh, uint flush);

#if SLIC_GET_STATS_ENABLED
static struct net_device_stats *slic_get_stats(struct net_device *dev);
#endif

static int slic_mac_set_address(struct net_device *dev, void *ptr);
static void slic_rcv_handler(struct adapter *adapter);
static void slic_link_event_handler(struct adapter *adapter);
static void slic_xmit_complete(struct adapter *adapter);
static void slic_upr_request_complete(struct adapter *adapter, u32 isr);
static int   slic_rspqueue_init(struct adapter *adapter);
static int   slic_rspqueue_reset(struct adapter *adapter);
static void  slic_rspqueue_free(struct adapter *adapter);
static struct slic_rspbuf *slic_rspqueue_getnext(struct adapter *adapter);
static void  slic_cmdqmem_init(struct adapter *adapter);
static void  slic_cmdqmem_free(struct adapter *adapter);
static u32 *slic_cmdqmem_addpage(struct adapter *adapter);
static int   slic_cmdq_init(struct adapter *adapter);
static void  slic_cmdq_free(struct adapter *adapter);
static void  slic_cmdq_reset(struct adapter *adapter);
static void  slic_cmdq_addcmdpage(struct adapter *adapter, u32 *page);
static void  slic_cmdq_getdone(struct adapter *adapter);
static void  slic_cmdq_putdone(struct adapter *adapter,
						struct slic_hostcmd *cmd);
static void  slic_cmdq_putdone_irq(struct adapter *adapter,
						struct slic_hostcmd *cmd);
static struct slic_hostcmd *slic_cmdq_getfree(struct adapter *adapter);
static int   slic_rcvqueue_init(struct adapter *adapter);
static int   slic_rcvqueue_reset(struct adapter *adapter);
static int   slic_rcvqueue_fill(struct adapter *adapter);
static u32 slic_rcvqueue_reinsert(struct adapter *adapter, struct sk_buff *skb);
static void  slic_rcvqueue_free(struct adapter *adapter);
static void slic_rcv_handle_error(struct adapter *adapter,
					struct slic_rcvbuf *rcvbuf);
static void slic_adapter_set_hwaddr(struct adapter *adapter);
static void slic_card_halt(struct sliccard *card, struct adapter *adapter);
static int slic_card_init(struct sliccard *card, struct adapter *adapter);
static void slic_intagg_set(struct adapter *adapter, u32 value);
static int  slic_card_download(struct adapter *adapter);
static u32 slic_card_locate(struct adapter *adapter);

static void slic_if_stop_queue(struct adapter *adapter);
static void slic_if_start_queue(struct adapter *adapter);
static int  slic_if_init(struct adapter *adapter);
static int  slic_adapter_allocresources(struct adapter *adapter);
static void slic_adapter_freeresources(struct adapter *adapter);
static void slic_link_config(struct adapter *adapter, u32 linkspeed,
			u32 linkduplex);
static void slic_unmap_mmio_space(struct adapter *adapter);
static void slic_card_cleanup(struct sliccard *card);
static void slic_init_cleanup(struct adapter *adapter);
static void slic_soft_reset(struct adapter *adapter);
static void slic_card_reset(struct adapter *adapter);
static bool slic_mac_filter(struct adapter *adapter,
			struct ether_header *ether_frame);
static void slic_mac_address_config(struct adapter *adapter);
static void slic_mac_config(struct adapter *adapter);
static void slic_mcast_set_mask(struct adapter *adapter);
static int slic_mcast_add_list(struct adapter *adapter, char *address);
static unsigned char slic_mcast_get_mac_hash(char *macaddr);
static void  slic_mcast_set_bit(struct adapter *adapter, char *address);
static void slic_config_set(struct adapter *adapter, bool linkchange);
static void slic_config_clear(struct adapter *adapter);
static void slic_config_get(struct adapter *adapter, u32 config,
			u32 configh);
static void slic_timer_get_stats(ulong device);
static void slic_timer_load_check(ulong context);
static void slic_timer_ping(ulong dev);
static void slic_assert_fail(void);
static ushort slic_eeprom_cksum(char *m, int len);
/* upr */
static void slic_upr_start(struct adapter *adapter);
static void slic_link_upr_complete(struct adapter *adapter, u32 Isr);
static int  slic_upr_request(struct adapter    *adapter,
			u32            upr_request,
			u32            upr_data,
			u32            upr_data_h,
			u32            upr_buffer,
			u32            upr_buffer_h);
static int  slic_upr_queue_request(struct adapter      *adapter,
				u32            upr_request,
				u32            upr_data,
				u32            upr_data_h,
				u32            upr_buffer,
				u32            upr_buffer_h);
static void slic_mcast_set_list(struct net_device *dev);
static void slic_mcast_init_crc32(void);

#if SLIC_DUMP_ENABLED
static int   slic_dump_thread(void *context);
static uint  slic_init_dump_thread(struct sliccard *card);
static unsigned char slic_get_dump_index(char *path);
static u32 slic_dump_card(struct sliccard *card, bool resume);
static u32 slic_dump_halt(struct sliccard *card, unsigned char proc);
static u32 slic_dump_reg(struct sliccard *card, unsigned char proc);
static u32 slic_dump_data(struct sliccard *card, u32 addr,
			ushort count, unsigned char desc);
static u32 slic_dump_queue(struct sliccard *card, u32 buf_phys,
			u32 buf_physh, u32 queue);
static u32 slic_dump_load_queue(struct sliccard *card, u32 data,
				u32 queue);
static u32 slic_dump_cam(struct sliccard *card, u32 addr,
			u32 count, unsigned char desc);

static u32 slic_dump_resume(struct sliccard *card, unsigned char proc);
static u32 slic_dump_send_cmd(struct sliccard *card, u32 cmd_phys,
				u32 cmd_physh, u32 buf_phys,
				u32 buf_physh);

#define create_file(x)         STATUS_SUCCESS
#define write_file(w, x, y, z) STATUS_SUCCESS
#define close_file(x)          STATUS_SUCCESS
#define read_file(w, x, y, z)  STATUS_SUCCESS
#define open_file(x)           STATUS_SUCCESS

/* PAGE_SIZE * 16 */
#define DUMP_PAGE_SIZE         0xFFFF
#define DUMP_PAGE_SIZE_HALF    0x7FFE
#endif

#endif /* _SLIC_INCLUDE_H_ */
