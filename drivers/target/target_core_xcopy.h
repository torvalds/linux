#include <target/target_core_base.h>

#define XCOPY_HDR_LEN			16
#define XCOPY_TARGET_DESC_LEN		32
#define XCOPY_SEGMENT_DESC_LEN		28
#define XCOPY_NAA_IEEE_REGEX_LEN	16
#define XCOPY_MAX_SECTORS		1024

/*
 * SPC4r37 6.4.6.1
 * Table 150 — CSCD descriptor ID values
 */
#define XCOPY_CSCD_DESC_ID_LIST_OFF_MAX	0x07FF

enum xcopy_origin_list {
	XCOL_SOURCE_RECV_OP = 0x01,
	XCOL_DEST_RECV_OP = 0x02,
};

struct xcopy_pt_cmd;

struct xcopy_op {
	int op_origin;

	struct se_cmd *xop_se_cmd;
	struct se_device *src_dev;
	unsigned char src_tid_wwn[XCOPY_NAA_IEEE_REGEX_LEN];
	struct se_device *dst_dev;
	unsigned char dst_tid_wwn[XCOPY_NAA_IEEE_REGEX_LEN];
	unsigned char local_dev_wwn[XCOPY_NAA_IEEE_REGEX_LEN];

	sector_t src_lba;
	sector_t dst_lba;
	unsigned short stdi;
	unsigned short dtdi;
	unsigned short nolb;
	unsigned int dbl;

	struct xcopy_pt_cmd *src_pt_cmd;
	struct xcopy_pt_cmd *dst_pt_cmd;

	u32 xop_data_nents;
	struct scatterlist *xop_data_sg;
	struct work_struct xop_work;
};

/*
 * Receive Copy Results Sevice Actions
 */
#define RCR_SA_COPY_STATUS		0x00
#define RCR_SA_RECEIVE_DATA		0x01
#define RCR_SA_OPERATING_PARAMETERS	0x03
#define RCR_SA_FAILED_SEGMENT_DETAILS	0x04

/*
 * Receive Copy Results defs for Operating Parameters
 */
#define RCR_OP_MAX_TARGET_DESC_COUNT	0x2
#define RCR_OP_MAX_SG_DESC_COUNT	0x1
#define RCR_OP_MAX_DESC_LIST_LEN	1024
#define RCR_OP_MAX_SEGMENT_LEN		268435456 /* 256 MB */
#define RCR_OP_TOTAL_CONCURR_COPIES	0x1 /* Must be <= 16384 */
#define RCR_OP_MAX_CONCURR_COPIES	0x1 /* Must be <= 255 */
#define RCR_OP_DATA_SEG_GRAN_LOG2	9 /* 512 bytes in log 2 */
#define RCR_OP_INLINE_DATA_GRAN_LOG2	9 /* 512 bytes in log 2 */
#define RCR_OP_HELD_DATA_GRAN_LOG2	9 /* 512 bytes in log 2 */

extern int target_xcopy_setup_pt(void);
extern void target_xcopy_release_pt(void);
extern sense_reason_t target_do_xcopy(struct se_cmd *);
extern sense_reason_t target_do_receive_copy_results(struct se_cmd *);
