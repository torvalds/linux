/*
 * linux/drivers/s390/net/qeth_fs.h
 *
 * Linux on zSeries OSA Express and HiperSockets support.
 *
 * This header file contains definitions related to sysfs and procfs.
 *
 * Copyright 2000,2003 IBM Corporation
 * Author(s): Thomas Spatzier <tspat@de.ibm.com>
 *
 */
#ifndef __QETH_FS_H__
#define __QETH_FS_H__

#define VERSION_QETH_FS_H "$Revision: 1.9 $"

extern const char *VERSION_QETH_PROC_C;
extern const char *VERSION_QETH_SYS_C;

#ifdef CONFIG_PROC_FS
extern int
qeth_create_procfs_entries(void);

extern void
qeth_remove_procfs_entries(void);
#else
static inline int
qeth_create_procfs_entries(void)
{
	return 0;
}

static inline void
qeth_remove_procfs_entries(void)
{
}
#endif /* CONFIG_PROC_FS */

extern int
qeth_create_device_attributes(struct device *dev);

extern void
qeth_remove_device_attributes(struct device *dev);

extern int
qeth_create_driver_attributes(void);

extern void
qeth_remove_driver_attributes(void);

/*
 * utility functions used in qeth_proc.c and qeth_sys.c
 */

static inline const char *
qeth_get_checksum_str(struct qeth_card *card)
{
	if (card->options.checksum_type == SW_CHECKSUMMING)
		return "sw";
	else if (card->options.checksum_type == HW_CHECKSUMMING)
		return "hw";
	else
		return "no";
}

static inline const char *
qeth_get_prioq_str(struct qeth_card *card, char *buf)
{
	if (card->qdio.do_prio_queueing == QETH_NO_PRIO_QUEUEING)
		sprintf(buf, "always_q_%i", card->qdio.default_out_queue);
	else
		strcpy(buf, (card->qdio.do_prio_queueing ==
					QETH_PRIO_Q_ING_PREC)?
				"by_prec." : "by_ToS");
	return buf;
}

static inline const char *
qeth_get_bufsize_str(struct qeth_card *card)
{
	if (card->qdio.in_buf_size == 16384)
		return "16k";
	else if (card->qdio.in_buf_size == 24576)
		return "24k";
	else if (card->qdio.in_buf_size == 32768)
		return "32k";
	else if (card->qdio.in_buf_size == 40960)
		return "40k";
	else
		return "64k";
}

static inline const char *
qeth_get_cardname(struct qeth_card *card)
{
 	if (card->info.guestlan) {
 		switch (card->info.type) {
 		case QETH_CARD_TYPE_OSAE:
			return " Guest LAN QDIO";
 		case QETH_CARD_TYPE_IQD:
			return " Guest LAN Hiper";
		default:
			return " unknown";
 		}
	} else {
		switch (card->info.type) {
		case QETH_CARD_TYPE_OSAE:
			return " OSD Express";
		case QETH_CARD_TYPE_IQD:
			return " HiperSockets";
		default:
			return " unknown";
		}
	}
	return " n/a";
}

/* max length to be returned: 14 */
static inline const char *
qeth_get_cardname_short(struct qeth_card *card)
{
	if (card->info.guestlan){
		switch (card->info.type){
		case QETH_CARD_TYPE_OSAE:
			return "GuestLAN QDIO";
		case QETH_CARD_TYPE_IQD:
			return "GuestLAN Hiper";
		default:
			return "unknown";
		}
	} else {
		switch (card->info.type) {
		case QETH_CARD_TYPE_OSAE:
			switch (card->info.link_type) {
			case QETH_LINK_TYPE_FAST_ETH:
				return "OSD_100";
			case QETH_LINK_TYPE_HSTR:
				return "HSTR";
			case QETH_LINK_TYPE_GBIT_ETH:
				return "OSD_1000";
			case QETH_LINK_TYPE_10GBIT_ETH:
				return "OSD_10GIG";
			case QETH_LINK_TYPE_LANE_ETH100:
				return "OSD_FE_LANE";
			case QETH_LINK_TYPE_LANE_TR:
				return "OSD_TR_LANE";
			case QETH_LINK_TYPE_LANE_ETH1000:
				return "OSD_GbE_LANE";
			case QETH_LINK_TYPE_LANE:
				return "OSD_ATM_LANE";
			default:
				return "OSD_Express";
			}
		case QETH_CARD_TYPE_IQD:
			return "HiperSockets";
		default:
			return "unknown";
		}
	}
	return "n/a";
}

#endif /* __QETH_FS_H__ */
