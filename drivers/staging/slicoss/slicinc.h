/**************************************************************************
 *
 * Copyright (c) 2000-2002 Alacritech, Inc.  All rights reserved.
 *
 * $Id: slicinc.h,v 1.4 2006/07/14 16:42:56 mook Exp $
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

int slic_entry_probe(struct pci_dev              *pcidev,
			const struct pci_device_id  *ent);
int slic_init(struct pci_dev           *pcidev,
	const struct pci_device_id     *pci_tbl_entry,
	long                      memaddr,
	int                       chip_idx,
	int                       acpi_idle_state);
void slic_entry_remove(struct pci_dev *pcidev);

void slic_init_driver(void);
int  slic_entry_open(struct net_device *dev);
int  slic_entry_halt(struct net_device *dev);
int  slic_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
int  slic_xmit_start(struct sk_buff *skb, struct net_device *dev);
void slic_xmit_fail(p_adapter_t        adapter,
			struct sk_buff   *skb,
			pvoid              cmd,
			ulong32              skbtype,
			ulong32              status);
void slic_xmit_timeout(struct net_device *dev);
void slic_config_pci(struct pci_dev *pcidev);
struct sk_buff *slic_rcvqueue_getnext(p_adapter_t  adapter);

inline void slic_reg32_write(void __iomem *reg, ulong32 value, uint flush);
inline void slic_reg64_write(p_adapter_t adapter, void __iomem *reg,
	ulong32 value, void __iomem *regh, ulong32 paddrh, uint flush);
inline ulong32 slic_reg32_read(pulong32 reg, uint flush);
inline ulong32 slic_reg16_read(pulong32 reg, uint flush);

#if SLIC_GET_STATS_ENABLED
struct net_device_stats *slic_get_stats(struct net_device *dev);
#endif

int slic_mac_set_address(struct net_device *dev, pvoid ptr);

int slicproc_card_read(char *page, char **start, off_t off, int count,
			int *eof, void *data);
int slicproc_card_write(struct file *file, const char __user *buffer,
			ulong count, void *data);
void slicproc_card_create(p_sliccard_t card);
void slicproc_card_destroy(p_sliccard_t card);
int slicproc_adapter_read(char *page, char **start, off_t off, int count,
			int *eof, void *data);
int slicproc_adapter_write(struct file *file, const char __user *buffer,
			ulong count, void *data);
void slicproc_adapter_create(p_adapter_t adapter);
void slicproc_adapter_destroy(p_adapter_t adapter);
void slicproc_create(void);
void slicproc_destroy(void);

void slic_interrupt_process(p_adapter_t  adapter, ulong32 isr);
void slic_rcv_handler(p_adapter_t  adapter);
void slic_upr_handler(p_adapter_t  adapter);
void slic_link_event_handler(p_adapter_t  adapter);
void slic_xmit_complete(p_adapter_t  adapter);
void slic_upr_request_complete(p_adapter_t  adapter, ulong32 isr);
int   slic_rspqueue_init(p_adapter_t  adapter);
int   slic_rspqueue_reset(p_adapter_t  adapter);
void  slic_rspqueue_free(p_adapter_t  adapter);
p_slic_rspbuf_t slic_rspqueue_getnext(p_adapter_t  adapter);
void  slic_cmdqmem_init(p_adapter_t  adapter);
void  slic_cmdqmem_free(p_adapter_t  adapter);
pulong32 slic_cmdqmem_addpage(p_adapter_t  adapter);
int   slic_cmdq_init(p_adapter_t  adapter);
void  slic_cmdq_free(p_adapter_t  adapter);
void  slic_cmdq_reset(p_adapter_t  adapter);
void  slic_cmdq_addcmdpage(p_adapter_t  adapter, pulong32 page);
void  slic_cmdq_getdone(p_adapter_t  adapter);
void  slic_cmdq_putdone(p_adapter_t  adapter, p_slic_hostcmd_t cmd);
void  slic_cmdq_putdone_irq(p_adapter_t  adapter, p_slic_hostcmd_t cmd);
p_slic_hostcmd_t slic_cmdq_getfree(p_adapter_t  adapter);
int   slic_rcvqueue_init(p_adapter_t  adapter);
int   slic_rcvqueue_reset(p_adapter_t  adapter);
int   slic_rcvqueue_fill(p_adapter_t  adapter);
ulong32 slic_rcvqueue_reinsert(p_adapter_t adapter, struct sk_buff *skb);
void  slic_rcvqueue_free(p_adapter_t  adapter);
void slic_rcv_handle_error(p_adapter_t adapter, p_slic_rcvbuf_t    rcvbuf);
void slic_adapter_set_hwaddr(p_adapter_t adapter);
void slic_card_halt(p_sliccard_t card, p_adapter_t adapter);
int slic_card_init(p_sliccard_t card, p_adapter_t adapter);
void slic_intagg_set(p_adapter_t  adapter, ulong32 value);
int  slic_card_download(p_adapter_t  adapter);
ulong32 slic_card_locate(p_adapter_t  adapter);
int  slic_card_removeadapter(p_adapter_t  adapter);
void slic_card_remaster(p_adapter_t  adapter);
void slic_card_softreset(p_adapter_t  adapter);
void slic_card_up(p_adapter_t  adapter);
void slic_card_down(p_adapter_t  adapter);

void slic_if_stop_queue(p_adapter_t adapter);
void slic_if_start_queue(p_adapter_t adapter);
int  slic_if_init(p_adapter_t  adapter);
void slic_adapter_close(p_adapter_t  adapter);
int  slic_adapter_allocresources(p_adapter_t  adapter);
void slic_adapter_freeresources(p_adapter_t  adapter);
void slic_link_config(p_adapter_t  adapter, ulong32 linkspeed,
			ulong32 linkduplex);
void slic_unmap_mmio_space(p_adapter_t adapter);
void slic_card_cleanup(p_sliccard_t card);
void slic_init_cleanup(p_adapter_t adapter);
void slic_card_reclaim_buffers(p_adapter_t adapter);
void slic_soft_reset(p_adapter_t adapter);
void slic_card_reset(p_adapter_t adapter);
boolean slic_mac_filter(p_adapter_t  adapter, p_ether_header ether_frame);
void slic_mac_address_config(p_adapter_t  adapter);
void slic_mac_config(p_adapter_t  adapter);
void slic_mcast_set_mask(p_adapter_t  adapter);
void slic_mac_setmcastaddrs(p_adapter_t  adapter);
int slic_mcast_add_list(p_adapter_t adapter, pchar address);
uchar slic_mcast_get_mac_hash(pchar macaddr);
void  slic_mcast_set_bit(p_adapter_t adapter, pchar address);
void slic_config_set(p_adapter_t adapter, boolean linkchange);
void slic_config_clear(p_adapter_t  adapter);
void slic_config_get(p_adapter_t  adapter, ulong32 config, ulong32 configh);
void slic_timer_get_stats(ulong device);
void slic_timer_load_check(ulong context);
void slic_timer_ping(ulong dev);
void slic_stall_msec(int stall);
void slic_stall_usec(int stall);
void slic_assert_fail(void);
ushort slic_eeprom_cksum(pchar m, int len);
/* upr */
void slic_upr_start(p_adapter_t  adapter);
void slic_link_upr_complete(p_adapter_t  adapter, ulong32 Isr);
int  slic_upr_request(p_adapter_t      adapter,
			ulong32            upr_request,
			ulong32            upr_data,
			ulong32            upr_data_h,
			ulong32            upr_buffer,
			ulong32            upr_buffer_h);
int  slic_upr_queue_request(p_adapter_t      adapter,
				ulong32            upr_request,
				ulong32            upr_data,
				ulong32            upr_data_h,
				ulong32            upr_buffer,
				ulong32            upr_buffer_h);
void slic_mcast_set_list(struct net_device *dev);
void  slic_mcast_init_crc32(void);

#if SLIC_DUMP_ENABLED
int   slic_dump_thread(void *context);
uint  slic_init_dump_thread(p_sliccard_t card);
uchar slic_get_dump_index(pchar path);
ulong32 slic_dump_card(p_sliccard_t card, boolean resume);
ulong32 slic_dump_halt(p_sliccard_t card, uchar proc);
ulong32 slic_dump_reg(p_sliccard_t card, uchar proc);
ulong32 slic_dump_data(p_sliccard_t card, ulong32 addr,
			ushort count, uchar desc);
ulong32 slic_dump_queue(p_sliccard_t card, ulong32 buf_phys,
			ulong32 buf_physh, ulong32 queue);
ulong32 slic_dump_load_queue(p_sliccard_t card, ulong32 data, ulong32 queue);
ulong32 slic_dump_cam(p_sliccard_t card, ulong32 addr,
			ulong32 count, uchar desc);

ulong32 slic_dump_resume(p_sliccard_t card, uchar proc);
ulong32 slic_dump_send_cmd(p_sliccard_t card, ulong32 cmd_phys,
				ulong32 cmd_physh, ulong32 buf_phys,
				ulong32 buf_physh);

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
