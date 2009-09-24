/* arch/arm/mach-msm/qdsp5/adsp_video_verify_cmd.c
 *
 * Verificion code for aDSP VDEC packets from userspace.
 *
 * Copyright (c) 2008 QUALCOMM Incorporated
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/io.h>

#define ADSP_DEBUG_MSGS 0
#if ADSP_DEBUG_MSGS
#define DLOG(fmt,args...) \
	do { printk(KERN_INFO "[%s:%s:%d] "fmt, __FILE__, __func__, __LINE__, \
	     ##args); } \
	while (0)
#else
#define DLOG(x...) do {} while (0)
#endif


#include <mach/qdsp5/qdsp5vdeccmdi.h>
#include "adsp.h"

static inline void *high_low_short_to_ptr(unsigned short high,
					  unsigned short low)
{
	return (void *)((((unsigned long)high) << 16) | ((unsigned long)low));
}

static inline void ptr_to_high_low_short(void *ptr, unsigned short *high,
					 unsigned short *low)
{
	*high = (unsigned short)((((unsigned long)ptr) >> 16) & 0xffff);
	*low = (unsigned short)((unsigned long)ptr & 0xffff);
}

static int pmem_fixup_high_low(unsigned short *high,
				unsigned short *low,
				unsigned short size_high,
				unsigned short size_low,
				struct msm_adsp_module *module,
				unsigned long *addr, unsigned long *size)
{
	void *phys_addr;
	unsigned long phys_size;
	unsigned long kvaddr;

	phys_addr = high_low_short_to_ptr(*high, *low);
	phys_size = (unsigned long)high_low_short_to_ptr(size_high, size_low);
	DLOG("virt %x %x\n", phys_addr, phys_size);
	if (adsp_pmem_fixup_kvaddr(module, &phys_addr, &kvaddr, phys_size)) {
		DLOG("ah%x al%x sh%x sl%x addr %x size %x\n",
			*high, *low, size_high, size_low, phys_addr, phys_size);
		return -1;
	}
	ptr_to_high_low_short(phys_addr, high, low);
	DLOG("phys %x %x\n", phys_addr, phys_size);
	if (addr)
		*addr = kvaddr;
	if (size)
		*size = phys_size;
	return 0;
}

static int verify_vdec_pkt_cmd(struct msm_adsp_module *module,
			       void *cmd_data, size_t cmd_size)
{
	unsigned short cmd_id = ((unsigned short *)cmd_data)[0];
	viddec_cmd_subframe_pkt *pkt;
	unsigned long subframe_pkt_addr;
	unsigned long subframe_pkt_size;
	viddec_cmd_frame_header_packet *frame_header_pkt;
	int i, num_addr, skip;
	unsigned short *frame_buffer_high, *frame_buffer_low;
	unsigned long frame_buffer_size;
	unsigned short frame_buffer_size_high, frame_buffer_size_low;

	DLOG("cmd_size %d cmd_id %d cmd_data %x\n", cmd_size, cmd_id, cmd_data);
	if (cmd_id != VIDDEC_CMD_SUBFRAME_PKT) {
		printk(KERN_INFO "adsp_video: unknown video packet %u\n",
			cmd_id);
		return 0;
	}
	if (cmd_size < sizeof(viddec_cmd_subframe_pkt))
		return -1;

	pkt = (viddec_cmd_subframe_pkt *)cmd_data;

	if (pmem_fixup_high_low(&(pkt->subframe_packet_high),
				&(pkt->subframe_packet_low),
				pkt->subframe_packet_size_high,
				pkt->subframe_packet_size_low,
				module,
				&subframe_pkt_addr,
				&subframe_pkt_size))
		return -1;

	/* deref those ptrs and check if they are a frame header packet */
	frame_header_pkt = (viddec_cmd_frame_header_packet *)subframe_pkt_addr;

	switch (frame_header_pkt->packet_id) {
	case 0xB201: /* h.264 */
		num_addr = skip = 8;
		break;
	case 0x4D01: /* mpeg-4 and h.263 */
		num_addr = 3;
		skip = 0;
		break;
	default:
		return 0;
	}

	frame_buffer_high = &frame_header_pkt->frame_buffer_0_high;
	frame_buffer_low = &frame_header_pkt->frame_buffer_0_low;
	frame_buffer_size = (frame_header_pkt->x_dimension *
			     frame_header_pkt->y_dimension * 3) / 2;
	ptr_to_high_low_short((void *)frame_buffer_size,
			      &frame_buffer_size_high,
			      &frame_buffer_size_low);
	for (i = 0; i < num_addr; i++) {
		if (pmem_fixup_high_low(frame_buffer_high, frame_buffer_low,
					frame_buffer_size_high,
					frame_buffer_size_low,
					module,
					NULL, NULL))
			return -1;
		frame_buffer_high += 2;
		frame_buffer_low += 2;
	}
	/* Patch the output buffer. */
	frame_buffer_high += 2*skip;
	frame_buffer_low += 2*skip;
	if (pmem_fixup_high_low(frame_buffer_high, frame_buffer_low,
				frame_buffer_size_high,
				frame_buffer_size_low, module, NULL, NULL))
		return -1;
	return 0;
}

int adsp_video_verify_cmd(struct msm_adsp_module *module,
			 unsigned int queue_id, void *cmd_data,
			 size_t cmd_size)
{
	switch (queue_id) {
	case QDSP_mpuVDecPktQueue:
		DLOG("\n");
		return verify_vdec_pkt_cmd(module, cmd_data, cmd_size);
	default:
		printk(KERN_INFO "unknown video queue %u\n", queue_id);
		return 0;
	}
}

