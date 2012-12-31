
#ifndef __CPRM_API_SAMSUNG
#define __CPRM_API_SAMSUNG

#define	SETRESP(x)	(x << 11)
#define	GETRESP(x)	((x >> 11) & 0x0007)

#define NORESP	SETRESP(0)	/* No response command */
#define R1RESP	SETRESP(1)	/* r1 response command */
#define R1BRESP	SETRESP(2)	/* r1b response command */
#define R2RESP	SETRESP(3)	/* r2 response command */
#define R3RESP	SETRESP(4)	/* r3 response command */
#define R6RESP	SETRESP(5)	/* r6 response command */
#define R7RESP	SETRESP(6)	/* r7 response command */

#define DT	0x8000	/* With data */
#define DIR_IN	0x0000	/* Data Transfer read */
#define DIR_OUT	0x4000	/* Data Transfer write */
#define ACMD	0x0400	/* Is ACMD	*/

#define ACMD6 (6+R1RESP+ACMD) /* Set Bus Width(SD) */
#define ACMD13 (13+R1RESP+ACMD+DT+DIR_IN) /* SD Status */
#define ACMD18 (18+R1RESP+ACMD+DT+DIR_IN) /* Secure Read Multi Block */
#define ACMD22 (22+R1RESP+ACMD+DT+DIR_IN) /* Send Number Write block */
#define ACMD23 (23+R1RESP+ACMD) /* Set Write block Erase Count */
#define ACMD25 (25+R1RESP+ACMD+DT+DIR_OUT) /* Secure Write Multiple Block */
#define ACMD26 (26+R1RESP+ACMD+DT+DIR_OUT) /* Secure Write MKB */
#define ACMD38 (38+R1BRESP+ACMD) /* Secure Erase */
#define ACMD41 (41+R3RESP+ACMD) /* Send App Operating Condition */
#define ACMD42 (42+R1RESP+ACMD) /* Set Clear Card Detect */
#define ACMD43 (43+R1RESP+ACMD+DT+DIR_IN) /* Get MKB */
#define ACMD44 (44+R1RESP+ACMD+DT+DIR_IN) /* Get MID */
#define ACMD45 (45+R1RESP+ACMD+DT+DIR_OUT) /* Set CER RN1 */
#define ACMD46 (46+R1RESP+ACMD+DT+DIR_IN) /* Get CER RN2 */
#define ACMD47 (47+R1RESP+ACMD+DT+DIR_OUT) /* Set CER RES2 */
#define ACMD48 (48+R1RESP+ACMD+DT+DIR_IN) /* Get CER RES1 */
#define ACMD49 (49+R1BRESP+ACMD) /* Change Erase Area */
#define ACMD51 (51+R1RESP+ACMD+DT+DIR_IN) /* Send SCR */

/* Application-specific commands supported by all SD cards */
enum SD_ACMD {
SD_ACMD6_SET_BUS_WIDTH = 6,
SD_ACMD13_SD_STATUS = 13,
SD_ACMD18_SECURE_READ_MULTI_BLOCK = 18,
SD_ACMD22_SEND_NUM_WR_BLOCKS = 22,
SD_ACMD23_SET_WR_BLK_ERASE_COUNT = 23,
SD_ACMD25_SECURE_WRITE_MULTI_BLOCK = 25,
SD_ACMD26_SECURE_WRITE_MKB = 26,
SD_ACMD38_SECURE_ERASE = 38,
SD_ACMD41_SD_APP_OP_COND = 41,
SD_ACMD42_SET_CLR_CARD_DETECT = 42,
SD_ACMD43_GET_MKB = 43,
SD_ACMD44_GET_MID = 44,
SD_ACMD45_SET_CER_RN1 = 45,
SD_ACMD46_GET_CER_RN2 = 46,
SD_ACMD47_SET_CER_RES2 = 47,
SD_ACMD48_GET_CER_RES1 = 48,
SD_ACMD49_CHANGE_SECURE_AREA = 49,
SD_ACMD51_SEND_SCR = 51
};

struct cprm_request {
	unsigned int	cmd;
	unsigned long	arg;
	unsigned char	*buff;
	unsigned int	len;
};

int stub_sendcmd(struct mmc_card *card,
	unsigned int cmd,
	unsigned long arg,
	unsigned int len,
	unsigned char *buff);

#endif /* __CPRM_API_SAMSUNG */
