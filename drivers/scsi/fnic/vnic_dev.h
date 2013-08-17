/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _VNIC_DEV_H_
#define _VNIC_DEV_H_

#include "vnic_resource.h"
#include "vnic_devcmd.h"

/*
 * These defines avoid symbol clash between fnic and enic (Cisco 10G Eth
 * Driver) when both are built with CONFIG options =y
 */
#define vnic_dev_priv fnic_dev_priv
#define vnic_dev_get_res_count fnic_dev_get_res_count
#define vnic_dev_get_res fnic_dev_get_res
#define vnic_dev_desc_ring_size fnic_dev_desc_ring_siz
#define vnic_dev_clear_desc_ring fnic_dev_clear_desc_ring
#define vnic_dev_alloc_desc_ring fnic_dev_alloc_desc_ring
#define vnic_dev_free_desc_ring fnic_dev_free_desc_ring
#define vnic_dev_cmd fnic_dev_cmd
#define vnic_dev_fw_info fnic_dev_fw_info
#define vnic_dev_spec fnic_dev_spec
#define vnic_dev_stats_clear fnic_dev_stats_clear
#define vnic_dev_stats_dump fnic_dev_stats_dump
#define vnic_dev_hang_notify fnic_dev_hang_notify
#define vnic_dev_packet_filter fnic_dev_packet_filter
#define vnic_dev_add_addr fnic_dev_add_addr
#define vnic_dev_del_addr fnic_dev_del_addr
#define vnic_dev_mac_addr fnic_dev_mac_addr
#define vnic_dev_notify_set fnic_dev_notify_set
#define vnic_dev_notify_unset fnic_dev_notify_unset
#define vnic_dev_link_status fnic_dev_link_status
#define vnic_dev_port_speed fnic_dev_port_speed
#define vnic_dev_msg_lvl fnic_dev_msg_lvl
#define vnic_dev_mtu fnic_dev_mtu
#define vnic_dev_link_down_cnt fnic_dev_link_down_cnt
#define vnic_dev_close fnic_dev_close
#define vnic_dev_enable fnic_dev_enable
#define vnic_dev_disable fnic_dev_disable
#define vnic_dev_open fnic_dev_open
#define vnic_dev_open_done fnic_dev_open_done
#define vnic_dev_init fnic_dev_init
#define vnic_dev_soft_reset fnic_dev_soft_reset
#define vnic_dev_soft_reset_done fnic_dev_soft_reset_done
#define vnic_dev_set_intr_mode fnic_dev_set_intr_mode
#define vnic_dev_get_intr_mode fnic_dev_get_intr_mode
#define vnic_dev_unregister fnic_dev_unregister
#define vnic_dev_register fnic_dev_register

#ifndef VNIC_PADDR_TARGET
#define VNIC_PADDR_TARGET	0x0000000000000000ULL
#endif

#ifndef readq
static inline u64 readq(void __iomem *reg)
{
	return ((u64)readl(reg + 0x4UL) << 32) | (u64)readl(reg);
}

static inline void writeq(u64 val, void __iomem *reg)
{
	writel(val & 0xffffffff, reg);
	writel(val >> 32, reg + 0x4UL);
}
#endif

enum vnic_dev_intr_mode {
	VNIC_DEV_INTR_MODE_UNKNOWN,
	VNIC_DEV_INTR_MODE_INTX,
	VNIC_DEV_INTR_MODE_MSI,
	VNIC_DEV_INTR_MODE_MSIX,
};

struct vnic_dev_bar {
	void __iomem *vaddr;
	dma_addr_t bus_addr;
	unsigned long len;
};

struct vnic_dev_ring {
	void *descs;
	size_t size;
	dma_addr_t base_addr;
	size_t base_align;
	void *descs_unaligned;
	size_t size_unaligned;
	dma_addr_t base_addr_unaligned;
	unsigned int desc_size;
	unsigned int desc_count;
	unsigned int desc_avail;
};

struct vnic_dev;
struct vnic_stats;

void *vnic_dev_priv(struct vnic_dev *vdev);
unsigned int vnic_dev_get_res_count(struct vnic_dev *vdev,
				    enum vnic_res_type type);
void __iomem *vnic_dev_get_res(struct vnic_dev *vdev, enum vnic_res_type type,
			       unsigned int index);
unsigned int vnic_dev_desc_ring_size(struct vnic_dev_ring *ring,
				     unsigned int desc_count,
				     unsigned int desc_size);
void vnic_dev_clear_desc_ring(struct vnic_dev_ring *ring);
int vnic_dev_alloc_desc_ring(struct vnic_dev *vdev, struct vnic_dev_ring *ring,
			     unsigned int desc_count, unsigned int desc_size);
void vnic_dev_free_desc_ring(struct vnic_dev *vdev,
			     struct vnic_dev_ring *ring);
int vnic_dev_cmd(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
		 u64 *a0, u64 *a1, int wait);
int vnic_dev_fw_info(struct vnic_dev *vdev,
		     struct vnic_devcmd_fw_info **fw_info);
int vnic_dev_spec(struct vnic_dev *vdev, unsigned int offset,
		  unsigned int size, void *value);
int vnic_dev_stats_clear(struct vnic_dev *vdev);
int vnic_dev_stats_dump(struct vnic_dev *vdev, struct vnic_stats **stats);
int vnic_dev_hang_notify(struct vnic_dev *vdev);
void vnic_dev_packet_filter(struct vnic_dev *vdev, int directed, int multicast,
			    int broadcast, int promisc, int allmulti);
void vnic_dev_add_addr(struct vnic_dev *vdev, u8 *addr);
void vnic_dev_del_addr(struct vnic_dev *vdev, u8 *addr);
int vnic_dev_mac_addr(struct vnic_dev *vdev, u8 *mac_addr);
int vnic_dev_notify_set(struct vnic_dev *vdev, u16 intr);
void vnic_dev_notify_unset(struct vnic_dev *vdev);
int vnic_dev_link_status(struct vnic_dev *vdev);
u32 vnic_dev_port_speed(struct vnic_dev *vdev);
u32 vnic_dev_msg_lvl(struct vnic_dev *vdev);
u32 vnic_dev_mtu(struct vnic_dev *vdev);
u32 vnic_dev_link_down_cnt(struct vnic_dev *vdev);
int vnic_dev_close(struct vnic_dev *vdev);
int vnic_dev_enable(struct vnic_dev *vdev);
int vnic_dev_disable(struct vnic_dev *vdev);
int vnic_dev_open(struct vnic_dev *vdev, int arg);
int vnic_dev_open_done(struct vnic_dev *vdev, int *done);
int vnic_dev_init(struct vnic_dev *vdev, int arg);
int vnic_dev_soft_reset(struct vnic_dev *vdev, int arg);
int vnic_dev_soft_reset_done(struct vnic_dev *vdev, int *done);
void vnic_dev_set_intr_mode(struct vnic_dev *vdev,
			    enum vnic_dev_intr_mode intr_mode);
enum vnic_dev_intr_mode vnic_dev_get_intr_mode(struct vnic_dev *vdev);
void vnic_dev_unregister(struct vnic_dev *vdev);
struct vnic_dev *vnic_dev_register(struct vnic_dev *vdev,
				   void *priv, struct pci_dev *pdev,
				   struct vnic_dev_bar *bar);

#endif /* _VNIC_DEV_H_ */
