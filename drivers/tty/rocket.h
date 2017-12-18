/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rocket.h --- the exported interface of the rocket driver to its configuration program.
 *
 * Written by Theodore Ts'o, Copyright 1997.
 * Copyright 1997 Comtrol Corporation. 
 *
 */

/*  Model Information Struct */
typedef struct {
	unsigned long model;
	char modelString[80];
	unsigned long numPorts;
	int loadrm2;
	int startingPortNumber;
} rocketModel_t;

struct rocket_config {
	int line;
	int flags;
	int closing_wait;
	int close_delay;
	int port;
	int reserved[32];
};

struct rocket_ports {
	int tty_major;
	int callout_major;
	rocketModel_t rocketModel[8];
};

struct rocket_version {
	char rocket_version[32];
	char rocket_date[32];
	char reserved[64];
};

/*
 * Rocketport flags
 */
/*#define ROCKET_CALLOUT_NOHUP    0x00000001 */
#define ROCKET_FORCE_CD		0x00000002
#define ROCKET_HUP_NOTIFY	0x00000004
#define ROCKET_SPLIT_TERMIOS	0x00000008
#define ROCKET_SPD_MASK		0x00000070
#define ROCKET_SPD_HI		0x00000010	/* Use 57600 instead of 38400 bps */
#define ROCKET_SPD_VHI		0x00000020	/* Use 115200 instead of 38400 bps */
#define ROCKET_SPD_SHI		0x00000030	/* Use 230400 instead of 38400 bps */
#define ROCKET_SPD_WARP	        0x00000040	/* Use 460800 instead of 38400 bps */
#define ROCKET_SAK		0x00000080
#define ROCKET_SESSION_LOCKOUT	0x00000100
#define ROCKET_PGRP_LOCKOUT	0x00000200
#define ROCKET_RTS_TOGGLE	0x00000400
#define ROCKET_MODE_MASK        0x00003000
#define ROCKET_MODE_RS232       0x00000000
#define ROCKET_MODE_RS485       0x00001000
#define ROCKET_MODE_RS422       0x00002000
#define ROCKET_FLAGS		0x00003FFF

#define ROCKET_USR_MASK 0x0071	/* Legal flags that non-privileged
				 * users can set or reset */

/*
 * For closing_wait and closing_wait2
 */
#define ROCKET_CLOSING_WAIT_NONE	ASYNC_CLOSING_WAIT_NONE
#define ROCKET_CLOSING_WAIT_INF		ASYNC_CLOSING_WAIT_INF

/*
 * Rocketport ioctls -- "RP"
 */
#define RCKP_GET_STRUCT		0x00525001
#define RCKP_GET_CONFIG		0x00525002
#define RCKP_SET_CONFIG		0x00525003
#define RCKP_GET_PORTS		0x00525004
#define RCKP_RESET_RM2		0x00525005
#define RCKP_GET_VERSION	0x00525006

/*  Rocketport Models */
#define MODEL_RP32INTF        0x0001	/* RP 32 port w/external I/F   */
#define MODEL_RP8INTF         0x0002	/* RP 8 port w/external I/F    */
#define MODEL_RP16INTF        0x0003	/* RP 16 port w/external I/F   */
#define MODEL_RP8OCTA         0x0005	/* RP 8 port w/octa cable      */
#define MODEL_RP4QUAD         0x0004	/* RP 4 port w/quad cable      */
#define MODEL_RP8J            0x0006	/* RP 8 port w/RJ11 connectors */
#define MODEL_RP4J            0x0007	/* RP 4 port w/RJ45 connectors */
#define MODEL_RP8SNI          0x0008	/* RP 8 port w/ DB78 SNI connector */
#define MODEL_RP16SNI         0x0009	/* RP 16 port w/ DB78 SNI connector */
#define MODEL_RPP4            0x000A	/* RP Plus 4 port              */
#define MODEL_RPP8            0x000B	/* RP Plus 8 port              */
#define MODEL_RP2_232         0x000E	/* RP Plus 2 port RS232        */
#define MODEL_RP2_422         0x000F	/* RP Plus 2 port RS232        */

/*  Rocketmodem II Models */
#define MODEL_RP6M            0x000C	/* RM 6 port                   */
#define MODEL_RP4M            0x000D	/* RM 4 port                   */

/* Universal PCI boards */
#define MODEL_UPCI_RP32INTF   0x0801	/* RP UPCI 32 port w/external I/F     */
#define MODEL_UPCI_RP8INTF    0x0802	/* RP UPCI 8 port w/external I/F      */
#define MODEL_UPCI_RP16INTF   0x0803	/* RP UPCI 16 port w/external I/F     */
#define MODEL_UPCI_RP8OCTA    0x0805	/* RP UPCI 8 port w/octa cable        */ 
#define MODEL_UPCI_RM3_8PORT  0x080C	/* RP UPCI Rocketmodem III 8 port     */
#define MODEL_UPCI_RM3_4PORT  0x080C	/* RP UPCI Rocketmodem III 4 port     */

/*  Compact PCI 16 port  */
#define MODEL_CPCI_RP16INTF   0x0903	/* RP Compact PCI 16 port w/external I/F */

/* All ISA boards */
#define MODEL_ISA             0x1000
