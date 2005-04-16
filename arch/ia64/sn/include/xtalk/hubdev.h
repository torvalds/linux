/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_XTALK_HUBDEV_H
#define _ASM_IA64_SN_XTALK_HUBDEV_H

#define HUB_WIDGET_ID_MAX 0xf
#define DEV_PER_WIDGET (2*2*8)
#define IIO_ITTE_WIDGET_BITS    4       /* size of widget field */
#define IIO_ITTE_WIDGET_MASK    ((1<<IIO_ITTE_WIDGET_BITS)-1)
#define IIO_ITTE_WIDGET_SHIFT   8

/*
 * Use the top big window as a surrogate for the first small window
 */
#define SWIN0_BIGWIN            HUB_NUM_BIG_WINDOW
#define IIO_NUM_ITTES   7
#define HUB_NUM_BIG_WINDOW      (IIO_NUM_ITTES - 1)

struct sn_flush_device_list {
	int sfdl_bus;
	int sfdl_slot;
	int sfdl_pin;
	struct bar_list {
		unsigned long start;
		unsigned long end;
	} sfdl_bar_list[6];
	unsigned long sfdl_force_int_addr;
	unsigned long sfdl_flush_value;
	volatile unsigned long *sfdl_flush_addr;
	uint64_t sfdl_persistent_busnum;
	struct pcibus_info *sfdl_pcibus_info;
	spinlock_t sfdl_flush_lock;
};

/*
 * **widget_p - Used as an array[wid_num][device] of sn_flush_device_list.
 */
struct sn_flush_nasid_entry  {
	struct sn_flush_device_list **widget_p; /* Used as a array of wid_num */
	uint64_t iio_itte[8];
};

struct hubdev_info {
	geoid_t				hdi_geoid;
	short				hdi_nasid;
	short				hdi_peer_nasid;   /* Dual Porting Peer */

	struct sn_flush_nasid_entry	hdi_flush_nasid_list;
	struct xwidget_info		hdi_xwidget_info[HUB_WIDGET_ID_MAX + 1];


	void				*hdi_nodepda;
	void				*hdi_node_vertex;
	void				*hdi_xtalk_vertex;
};

extern void hubdev_init_node(nodepda_t *, cnodeid_t);
extern void hub_error_init(struct hubdev_info *);
extern void ice_error_init(struct hubdev_info *);


#endif /* _ASM_IA64_SN_XTALK_HUBDEV_H */
