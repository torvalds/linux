/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Helper functions for scsw access.
 *
 *    Copyright IBM Corp. 2008, 2012
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#ifndef _ASM_S390_SCSW_H_
#define _ASM_S390_SCSW_H_

#include <linux/types.h>
#include <asm/css_chars.h>
#include <asm/cio.h>

/**
 * struct cmd_scsw - command-mode subchannel status word
 * @key: subchannel key
 * @sctl: suspend control
 * @eswf: esw format
 * @cc: deferred condition code
 * @fmt: format
 * @pfch: prefetch
 * @isic: initial-status interruption control
 * @alcc: address-limit checking control
 * @ssi: suppress-suspended interruption
 * @zcc: zero condition code
 * @ectl: extended control
 * @pno: path not operational
 * @res: reserved
 * @fctl: function control
 * @actl: activity control
 * @stctl: status control
 * @cpa: channel program address
 * @dstat: device status
 * @cstat: subchannel status
 * @count: residual count
 */
struct cmd_scsw {
	__u32 key  : 4;
	__u32 sctl : 1;
	__u32 eswf : 1;
	__u32 cc   : 2;
	__u32 fmt  : 1;
	__u32 pfch : 1;
	__u32 isic : 1;
	__u32 alcc : 1;
	__u32 ssi  : 1;
	__u32 zcc  : 1;
	__u32 ectl : 1;
	__u32 pno  : 1;
	__u32 res  : 1;
	__u32 fctl : 3;
	__u32 actl : 7;
	__u32 stctl : 5;
	__u32 cpa;
	__u32 dstat : 8;
	__u32 cstat : 8;
	__u32 count : 16;
} __attribute__ ((packed));

/**
 * struct tm_scsw - transport-mode subchannel status word
 * @key: subchannel key
 * @eswf: esw format
 * @cc: deferred condition code
 * @fmt: format
 * @x: IRB-format control
 * @q: interrogate-complete
 * @ectl: extended control
 * @pno: path not operational
 * @fctl: function control
 * @actl: activity control
 * @stctl: status control
 * @tcw: TCW address
 * @dstat: device status
 * @cstat: subchannel status
 * @fcxs: FCX status
 * @schxs: subchannel-extended status
 */
struct tm_scsw {
	u32 key:4;
	u32 :1;
	u32 eswf:1;
	u32 cc:2;
	u32 fmt:3;
	u32 x:1;
	u32 q:1;
	u32 :1;
	u32 ectl:1;
	u32 pno:1;
	u32 :1;
	u32 fctl:3;
	u32 actl:7;
	u32 stctl:5;
	u32 tcw;
	u32 dstat:8;
	u32 cstat:8;
	u32 fcxs:8;
	u32 ifob:1;
	u32 sesq:7;
} __attribute__ ((packed));

/**
 * struct eadm_scsw - subchannel status word for eadm subchannels
 * @key: subchannel key
 * @eswf: esw format
 * @cc: deferred condition code
 * @ectl: extended control
 * @fctl: function control
 * @actl: activity control
 * @stctl: status control
 * @aob: AOB address
 * @dstat: device status
 * @cstat: subchannel status
 */
struct eadm_scsw {
	u32 key:4;
	u32:1;
	u32 eswf:1;
	u32 cc:2;
	u32:6;
	u32 ectl:1;
	u32:2;
	u32 fctl:3;
	u32 actl:7;
	u32 stctl:5;
	u32 aob;
	u32 dstat:8;
	u32 cstat:8;
	u32:16;
} __packed;

/**
 * union scsw - subchannel status word
 * @cmd: command-mode SCSW
 * @tm: transport-mode SCSW
 * @eadm: eadm SCSW
 */
union scsw {
	struct cmd_scsw cmd;
	struct tm_scsw tm;
	struct eadm_scsw eadm;
} __packed;

#define SCSW_FCTL_CLEAR_FUNC	 0x1
#define SCSW_FCTL_HALT_FUNC	 0x2
#define SCSW_FCTL_START_FUNC	 0x4

#define SCSW_ACTL_SUSPENDED	 0x1
#define SCSW_ACTL_DEVACT	 0x2
#define SCSW_ACTL_SCHACT	 0x4
#define SCSW_ACTL_CLEAR_PEND	 0x8
#define SCSW_ACTL_HALT_PEND	 0x10
#define SCSW_ACTL_START_PEND	 0x20
#define SCSW_ACTL_RESUME_PEND	 0x40

#define SCSW_STCTL_STATUS_PEND	 0x1
#define SCSW_STCTL_SEC_STATUS	 0x2
#define SCSW_STCTL_PRIM_STATUS	 0x4
#define SCSW_STCTL_INTER_STATUS	 0x8
#define SCSW_STCTL_ALERT_STATUS	 0x10

#define DEV_STAT_ATTENTION	 0x80
#define DEV_STAT_STAT_MOD	 0x40
#define DEV_STAT_CU_END		 0x20
#define DEV_STAT_BUSY		 0x10
#define DEV_STAT_CHN_END	 0x08
#define DEV_STAT_DEV_END	 0x04
#define DEV_STAT_UNIT_CHECK	 0x02
#define DEV_STAT_UNIT_EXCEP	 0x01

#define SCHN_STAT_PCI		 0x80
#define SCHN_STAT_INCORR_LEN	 0x40
#define SCHN_STAT_PROG_CHECK	 0x20
#define SCHN_STAT_PROT_CHECK	 0x10
#define SCHN_STAT_CHN_DATA_CHK	 0x08
#define SCHN_STAT_CHN_CTRL_CHK	 0x04
#define SCHN_STAT_INTF_CTRL_CHK	 0x02
#define SCHN_STAT_CHAIN_CHECK	 0x01

#define SCSW_SESQ_DEV_NOFCX	 3
#define SCSW_SESQ_PATH_NOFCX	 4

/*
 * architectured values for first sense byte
 */
#define SNS0_CMD_REJECT		0x80
#define SNS_CMD_REJECT		SNS0_CMD_REJEC
#define SNS0_INTERVENTION_REQ	0x40
#define SNS0_BUS_OUT_CHECK	0x20
#define SNS0_EQUIPMENT_CHECK	0x10
#define SNS0_DATA_CHECK		0x08
#define SNS0_OVERRUN		0x04
#define SNS0_INCOMPL_DOMAIN	0x01

/*
 * architectured values for second sense byte
 */
#define SNS1_PERM_ERR		0x80
#define SNS1_INV_TRACK_FORMAT	0x40
#define SNS1_EOC		0x20
#define SNS1_MESSAGE_TO_OPER	0x10
#define SNS1_NO_REC_FOUND	0x08
#define SNS1_FILE_PROTECTED	0x04
#define SNS1_WRITE_INHIBITED	0x02
#define SNS1_INPRECISE_END	0x01

/*
 * architectured values for third sense byte
 */
#define SNS2_REQ_INH_WRITE	0x80
#define SNS2_CORRECTABLE	0x40
#define SNS2_FIRST_LOG_ERR	0x20
#define SNS2_ENV_DATA_PRESENT	0x10
#define SNS2_INPRECISE_END	0x04

/*
 * architectured values for PPRC errors
 */
#define SNS7_INVALID_ON_SEC	0x0e

/**
 * scsw_is_tm - check for transport mode scsw
 * @scsw: pointer to scsw
 *
 * Return non-zero if the specified scsw is a transport mode scsw, zero
 * otherwise.
 */
static inline int scsw_is_tm(union scsw *scsw)
{
	return css_general_characteristics.fcx && (scsw->tm.x == 1);
}

/**
 * scsw_key - return scsw key field
 * @scsw: pointer to scsw
 *
 * Return the value of the key field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_key(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.key;
	else
		return scsw->cmd.key;
}

/**
 * scsw_eswf - return scsw eswf field
 * @scsw: pointer to scsw
 *
 * Return the value of the eswf field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_eswf(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.eswf;
	else
		return scsw->cmd.eswf;
}

/**
 * scsw_cc - return scsw cc field
 * @scsw: pointer to scsw
 *
 * Return the value of the cc field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_cc(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.cc;
	else
		return scsw->cmd.cc;
}

/**
 * scsw_ectl - return scsw ectl field
 * @scsw: pointer to scsw
 *
 * Return the value of the ectl field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_ectl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.ectl;
	else
		return scsw->cmd.ectl;
}

/**
 * scsw_pno - return scsw pno field
 * @scsw: pointer to scsw
 *
 * Return the value of the pno field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_pno(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.pno;
	else
		return scsw->cmd.pno;
}

/**
 * scsw_fctl - return scsw fctl field
 * @scsw: pointer to scsw
 *
 * Return the value of the fctl field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_fctl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.fctl;
	else
		return scsw->cmd.fctl;
}

/**
 * scsw_actl - return scsw actl field
 * @scsw: pointer to scsw
 *
 * Return the value of the actl field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_actl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.actl;
	else
		return scsw->cmd.actl;
}

/**
 * scsw_stctl - return scsw stctl field
 * @scsw: pointer to scsw
 *
 * Return the value of the stctl field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_stctl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.stctl;
	else
		return scsw->cmd.stctl;
}

/**
 * scsw_dstat - return scsw dstat field
 * @scsw: pointer to scsw
 *
 * Return the value of the dstat field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_dstat(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.dstat;
	else
		return scsw->cmd.dstat;
}

/**
 * scsw_cstat - return scsw cstat field
 * @scsw: pointer to scsw
 *
 * Return the value of the cstat field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
static inline u32 scsw_cstat(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.cstat;
	else
		return scsw->cmd.cstat;
}

/**
 * scsw_cmd_is_valid_key - check key field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the key field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_key(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}

/**
 * scsw_cmd_is_valid_sctl - check sctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the sctl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_sctl(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}

/**
 * scsw_cmd_is_valid_eswf - check eswf field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the eswf field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_eswf(union scsw *scsw)
{
	return (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND);
}

/**
 * scsw_cmd_is_valid_cc - check cc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cc field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_cc(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC) &&
	       (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND);
}

/**
 * scsw_cmd_is_valid_fmt - check fmt field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fmt field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_fmt(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}

/**
 * scsw_cmd_is_valid_pfch - check pfch field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the pfch field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_pfch(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}

/**
 * scsw_cmd_is_valid_isic - check isic field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the isic field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_isic(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}

/**
 * scsw_cmd_is_valid_alcc - check alcc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the alcc field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_alcc(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}

/**
 * scsw_cmd_is_valid_ssi - check ssi field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the ssi field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_ssi(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}

/**
 * scsw_cmd_is_valid_zcc - check zcc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the zcc field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_zcc(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC) &&
	       (scsw->cmd.stctl & SCSW_STCTL_INTER_STATUS);
}

/**
 * scsw_cmd_is_valid_ectl - check ectl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the ectl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_ectl(union scsw *scsw)
{
	/* Must be status pending. */
	if (!(scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND))
		return 0;

	/* Must have alert status. */
	if (!(scsw->cmd.stctl & SCSW_STCTL_ALERT_STATUS))
		return 0;

	/* Must be alone or together with primary, secondary or both,
	 * => no intermediate status.
	 */
	if (scsw->cmd.stctl & SCSW_STCTL_INTER_STATUS)
		return 0;

	return 1;
}

/**
 * scsw_cmd_is_valid_pno - check pno field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the pno field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_pno(union scsw *scsw)
{
	/* Must indicate at least one I/O function. */
	if (!scsw->cmd.fctl)
		return 0;

	/* Must be status pending. */
	if (!(scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND))
		return 0;

	/* Can be status pending alone, or with any combination of primary,
	 * secondary and alert => no intermediate status.
	 */
	if (!(scsw->cmd.stctl & SCSW_STCTL_INTER_STATUS))
		return 1;

	/* If intermediate, must be suspended. */
	if (scsw->cmd.actl & SCSW_ACTL_SUSPENDED)
		return 1;

	return 0;
}

/**
 * scsw_cmd_is_valid_fctl - check fctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fctl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_fctl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}

/**
 * scsw_cmd_is_valid_actl - check actl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the actl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_actl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}

/**
 * scsw_cmd_is_valid_stctl - check stctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the stctl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_stctl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}

/**
 * scsw_cmd_is_valid_dstat - check dstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the dstat field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_dstat(union scsw *scsw)
{
	return (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (scsw->cmd.cc != 3);
}

/**
 * scsw_cmd_is_valid_cstat - check cstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cstat field of the specified command mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_cmd_is_valid_cstat(union scsw *scsw)
{
	return (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (scsw->cmd.cc != 3);
}

/**
 * scsw_tm_is_valid_key - check key field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the key field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_key(union scsw *scsw)
{
	return (scsw->tm.fctl & SCSW_FCTL_START_FUNC);
}

/**
 * scsw_tm_is_valid_eswf - check eswf field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the eswf field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_eswf(union scsw *scsw)
{
	return (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND);
}

/**
 * scsw_tm_is_valid_cc - check cc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cc field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_cc(union scsw *scsw)
{
	return (scsw->tm.fctl & SCSW_FCTL_START_FUNC) &&
	       (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND);
}

/**
 * scsw_tm_is_valid_fmt - check fmt field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fmt field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_fmt(union scsw *scsw)
{
	return 1;
}

/**
 * scsw_tm_is_valid_x - check x field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the x field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_x(union scsw *scsw)
{
	return 1;
}

/**
 * scsw_tm_is_valid_q - check q field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the q field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_q(union scsw *scsw)
{
	return 1;
}

/**
 * scsw_tm_is_valid_ectl - check ectl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the ectl field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_ectl(union scsw *scsw)
{
	/* Must be status pending. */
	if (!(scsw->tm.stctl & SCSW_STCTL_STATUS_PEND))
		return 0;

	/* Must have alert status. */
	if (!(scsw->tm.stctl & SCSW_STCTL_ALERT_STATUS))
		return 0;

	/* Must be alone or together with primary, secondary or both,
	 * => no intermediate status.
	 */
	if (scsw->tm.stctl & SCSW_STCTL_INTER_STATUS)
		return 0;

	return 1;
}

/**
 * scsw_tm_is_valid_pno - check pno field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the pno field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_pno(union scsw *scsw)
{
	/* Must indicate at least one I/O function. */
	if (!scsw->tm.fctl)
		return 0;

	/* Must be status pending. */
	if (!(scsw->tm.stctl & SCSW_STCTL_STATUS_PEND))
		return 0;

	/* Can be status pending alone, or with any combination of primary,
	 * secondary and alert => no intermediate status.
	 */
	if (!(scsw->tm.stctl & SCSW_STCTL_INTER_STATUS))
		return 1;

	/* If intermediate, must be suspended. */
	if (scsw->tm.actl & SCSW_ACTL_SUSPENDED)
		return 1;

	return 0;
}

/**
 * scsw_tm_is_valid_fctl - check fctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fctl field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_fctl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}

/**
 * scsw_tm_is_valid_actl - check actl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the actl field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_actl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}

/**
 * scsw_tm_is_valid_stctl - check stctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the stctl field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_stctl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}

/**
 * scsw_tm_is_valid_dstat - check dstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the dstat field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_dstat(union scsw *scsw)
{
	return (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (scsw->tm.cc != 3);
}

/**
 * scsw_tm_is_valid_cstat - check cstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cstat field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_cstat(union scsw *scsw)
{
	return (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (scsw->tm.cc != 3);
}

/**
 * scsw_tm_is_valid_fcxs - check fcxs field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fcxs field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_fcxs(union scsw *scsw)
{
	return 1;
}

/**
 * scsw_tm_is_valid_schxs - check schxs field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the schxs field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
static inline int scsw_tm_is_valid_schxs(union scsw *scsw)
{
	return (scsw->tm.cstat & (SCHN_STAT_PROG_CHECK |
				  SCHN_STAT_INTF_CTRL_CHK |
				  SCHN_STAT_PROT_CHECK |
				  SCHN_STAT_CHN_DATA_CHK));
}

/**
 * scsw_is_valid_actl - check actl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the actl field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_actl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_actl(scsw);
	else
		return scsw_cmd_is_valid_actl(scsw);
}

/**
 * scsw_is_valid_cc - check cc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cc field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_cc(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_cc(scsw);
	else
		return scsw_cmd_is_valid_cc(scsw);
}

/**
 * scsw_is_valid_cstat - check cstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cstat field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_cstat(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_cstat(scsw);
	else
		return scsw_cmd_is_valid_cstat(scsw);
}

/**
 * scsw_is_valid_dstat - check dstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the dstat field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_dstat(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_dstat(scsw);
	else
		return scsw_cmd_is_valid_dstat(scsw);
}

/**
 * scsw_is_valid_ectl - check ectl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the ectl field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_ectl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_ectl(scsw);
	else
		return scsw_cmd_is_valid_ectl(scsw);
}

/**
 * scsw_is_valid_eswf - check eswf field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the eswf field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_eswf(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_eswf(scsw);
	else
		return scsw_cmd_is_valid_eswf(scsw);
}

/**
 * scsw_is_valid_fctl - check fctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fctl field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_fctl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_fctl(scsw);
	else
		return scsw_cmd_is_valid_fctl(scsw);
}

/**
 * scsw_is_valid_key - check key field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the key field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_key(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_key(scsw);
	else
		return scsw_cmd_is_valid_key(scsw);
}

/**
 * scsw_is_valid_pno - check pno field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the pno field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_pno(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_pno(scsw);
	else
		return scsw_cmd_is_valid_pno(scsw);
}

/**
 * scsw_is_valid_stctl - check stctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the stctl field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
static inline int scsw_is_valid_stctl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_stctl(scsw);
	else
		return scsw_cmd_is_valid_stctl(scsw);
}

/**
 * scsw_cmd_is_solicited - check for solicited scsw
 * @scsw: pointer to scsw
 *
 * Return non-zero if the command mode scsw indicates that the associated
 * status condition is solicited, zero if it is unsolicited.
 */
static inline int scsw_cmd_is_solicited(union scsw *scsw)
{
	return (scsw->cmd.cc != 0) || (scsw->cmd.stctl !=
		(SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS));
}

/**
 * scsw_tm_is_solicited - check for solicited scsw
 * @scsw: pointer to scsw
 *
 * Return non-zero if the transport mode scsw indicates that the associated
 * status condition is solicited, zero if it is unsolicited.
 */
static inline int scsw_tm_is_solicited(union scsw *scsw)
{
	return (scsw->tm.cc != 0) || (scsw->tm.stctl !=
		(SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS));
}

/**
 * scsw_is_solicited - check for solicited scsw
 * @scsw: pointer to scsw
 *
 * Return non-zero if the transport or command mode scsw indicates that the
 * associated status condition is solicited, zero if it is unsolicited.
 */
static inline int scsw_is_solicited(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_solicited(scsw);
	else
		return scsw_cmd_is_solicited(scsw);
}

#endif /* _ASM_S390_SCSW_H_ */
