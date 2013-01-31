#ifndef _ASM_S390_CLP_H
#define _ASM_S390_CLP_H

/* CLP common request & response block size */
#define CLP_BLK_SIZE			PAGE_SIZE

struct clp_req_hdr {
	u16 len;
	u16 cmd;
} __packed;

struct clp_rsp_hdr {
	u16 len;
	u16 rsp;
} __packed;

/* CLP Response Codes */
#define CLP_RC_OK			0x0010	/* Command request successfully */
#define CLP_RC_CMD			0x0020	/* Command code not recognized */
#define CLP_RC_PERM			0x0030	/* Command not authorized */
#define CLP_RC_FMT			0x0040	/* Invalid command request format */
#define CLP_RC_LEN			0x0050	/* Invalid command request length */
#define CLP_RC_8K			0x0060	/* Command requires 8K LPCB */
#define CLP_RC_RESNOT0			0x0070	/* Reserved field not zero */
#define CLP_RC_NODATA			0x0080	/* No data available */
#define CLP_RC_FC_UNKNOWN		0x0100	/* Function code not recognized */

#endif
