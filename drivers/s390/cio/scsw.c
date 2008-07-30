/*
 *  Helper functions for scsw access.
 *
 *    Copyright IBM Corp. 2008
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/types.h>
#include <linux/module.h>
#include <asm/cio.h>
#include "css.h"
#include "chsc.h"

/**
 * scsw_is_tm - check for transport mode scsw
 * @scsw: pointer to scsw
 *
 * Return non-zero if the specified scsw is a transport mode scsw, zero
 * otherwise.
 */
int scsw_is_tm(union scsw *scsw)
{
	return css_general_characteristics.fcx && (scsw->tm.x == 1);
}
EXPORT_SYMBOL(scsw_is_tm);

/**
 * scsw_key - return scsw key field
 * @scsw: pointer to scsw
 *
 * Return the value of the key field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_key(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.key;
	else
		return scsw->cmd.key;
}
EXPORT_SYMBOL(scsw_key);

/**
 * scsw_eswf - return scsw eswf field
 * @scsw: pointer to scsw
 *
 * Return the value of the eswf field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_eswf(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.eswf;
	else
		return scsw->cmd.eswf;
}
EXPORT_SYMBOL(scsw_eswf);

/**
 * scsw_cc - return scsw cc field
 * @scsw: pointer to scsw
 *
 * Return the value of the cc field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_cc(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.cc;
	else
		return scsw->cmd.cc;
}
EXPORT_SYMBOL(scsw_cc);

/**
 * scsw_ectl - return scsw ectl field
 * @scsw: pointer to scsw
 *
 * Return the value of the ectl field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_ectl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.ectl;
	else
		return scsw->cmd.ectl;
}
EXPORT_SYMBOL(scsw_ectl);

/**
 * scsw_pno - return scsw pno field
 * @scsw: pointer to scsw
 *
 * Return the value of the pno field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_pno(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.pno;
	else
		return scsw->cmd.pno;
}
EXPORT_SYMBOL(scsw_pno);

/**
 * scsw_fctl - return scsw fctl field
 * @scsw: pointer to scsw
 *
 * Return the value of the fctl field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_fctl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.fctl;
	else
		return scsw->cmd.fctl;
}
EXPORT_SYMBOL(scsw_fctl);

/**
 * scsw_actl - return scsw actl field
 * @scsw: pointer to scsw
 *
 * Return the value of the actl field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_actl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.actl;
	else
		return scsw->cmd.actl;
}
EXPORT_SYMBOL(scsw_actl);

/**
 * scsw_stctl - return scsw stctl field
 * @scsw: pointer to scsw
 *
 * Return the value of the stctl field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_stctl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.stctl;
	else
		return scsw->cmd.stctl;
}
EXPORT_SYMBOL(scsw_stctl);

/**
 * scsw_dstat - return scsw dstat field
 * @scsw: pointer to scsw
 *
 * Return the value of the dstat field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_dstat(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.dstat;
	else
		return scsw->cmd.dstat;
}
EXPORT_SYMBOL(scsw_dstat);

/**
 * scsw_cstat - return scsw cstat field
 * @scsw: pointer to scsw
 *
 * Return the value of the cstat field of the specified scsw, regardless of
 * whether it is a transport mode or command mode scsw.
 */
u32 scsw_cstat(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw->tm.cstat;
	else
		return scsw->cmd.cstat;
}
EXPORT_SYMBOL(scsw_cstat);

/**
 * scsw_cmd_is_valid_key - check key field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the key field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_key(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_key);

/**
 * scsw_cmd_is_valid_sctl - check fctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fctl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_sctl(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_sctl);

/**
 * scsw_cmd_is_valid_eswf - check eswf field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the eswf field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_eswf(union scsw *scsw)
{
	return (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_eswf);

/**
 * scsw_cmd_is_valid_cc - check cc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cc field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_cc(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC) &&
	       (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_cc);

/**
 * scsw_cmd_is_valid_fmt - check fmt field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fmt field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_fmt(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_fmt);

/**
 * scsw_cmd_is_valid_pfch - check pfch field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the pfch field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_pfch(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_pfch);

/**
 * scsw_cmd_is_valid_isic - check isic field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the isic field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_isic(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_isic);

/**
 * scsw_cmd_is_valid_alcc - check alcc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the alcc field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_alcc(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_alcc);

/**
 * scsw_cmd_is_valid_ssi - check ssi field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the ssi field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_ssi(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_ssi);

/**
 * scsw_cmd_is_valid_zcc - check zcc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the zcc field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_zcc(union scsw *scsw)
{
	return (scsw->cmd.fctl & SCSW_FCTL_START_FUNC) &&
	       (scsw->cmd.stctl & SCSW_STCTL_INTER_STATUS);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_zcc);

/**
 * scsw_cmd_is_valid_ectl - check ectl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the ectl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_ectl(union scsw *scsw)
{
	return (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND) &&
	       !(scsw->cmd.stctl & SCSW_STCTL_INTER_STATUS) &&
	       (scsw->cmd.stctl & SCSW_STCTL_ALERT_STATUS);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_ectl);

/**
 * scsw_cmd_is_valid_pno - check pno field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the pno field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_pno(union scsw *scsw)
{
	return (scsw->cmd.fctl != 0) &&
	       (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (!(scsw->cmd.stctl & SCSW_STCTL_INTER_STATUS) ||
		 ((scsw->cmd.stctl & SCSW_STCTL_INTER_STATUS) &&
		  (scsw->cmd.actl & SCSW_ACTL_SUSPENDED)));
}
EXPORT_SYMBOL(scsw_cmd_is_valid_pno);

/**
 * scsw_cmd_is_valid_fctl - check fctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fctl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_fctl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}
EXPORT_SYMBOL(scsw_cmd_is_valid_fctl);

/**
 * scsw_cmd_is_valid_actl - check actl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the actl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_actl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}
EXPORT_SYMBOL(scsw_cmd_is_valid_actl);

/**
 * scsw_cmd_is_valid_stctl - check stctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the stctl field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_stctl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}
EXPORT_SYMBOL(scsw_cmd_is_valid_stctl);

/**
 * scsw_cmd_is_valid_dstat - check dstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the dstat field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_dstat(union scsw *scsw)
{
	return (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (scsw->cmd.cc != 3);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_dstat);

/**
 * scsw_cmd_is_valid_cstat - check cstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cstat field of the specified command mode scsw is
 * valid, zero otherwise.
 */
int scsw_cmd_is_valid_cstat(union scsw *scsw)
{
	return (scsw->cmd.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (scsw->cmd.cc != 3);
}
EXPORT_SYMBOL(scsw_cmd_is_valid_cstat);

/**
 * scsw_tm_is_valid_key - check key field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the key field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_key(union scsw *scsw)
{
	return (scsw->tm.fctl & SCSW_FCTL_START_FUNC);
}
EXPORT_SYMBOL(scsw_tm_is_valid_key);

/**
 * scsw_tm_is_valid_eswf - check eswf field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the eswf field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_eswf(union scsw *scsw)
{
	return (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND);
}
EXPORT_SYMBOL(scsw_tm_is_valid_eswf);

/**
 * scsw_tm_is_valid_cc - check cc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cc field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_cc(union scsw *scsw)
{
	return (scsw->tm.fctl & SCSW_FCTL_START_FUNC) &&
	       (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND);
}
EXPORT_SYMBOL(scsw_tm_is_valid_cc);

/**
 * scsw_tm_is_valid_fmt - check fmt field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fmt field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_fmt(union scsw *scsw)
{
	return 1;
}
EXPORT_SYMBOL(scsw_tm_is_valid_fmt);

/**
 * scsw_tm_is_valid_x - check x field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the x field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_x(union scsw *scsw)
{
	return 1;
}
EXPORT_SYMBOL(scsw_tm_is_valid_x);

/**
 * scsw_tm_is_valid_q - check q field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the q field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_q(union scsw *scsw)
{
	return 1;
}
EXPORT_SYMBOL(scsw_tm_is_valid_q);

/**
 * scsw_tm_is_valid_ectl - check ectl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the ectl field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_ectl(union scsw *scsw)
{
	return (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND) &&
	       !(scsw->tm.stctl & SCSW_STCTL_INTER_STATUS) &&
	       (scsw->tm.stctl & SCSW_STCTL_ALERT_STATUS);
}
EXPORT_SYMBOL(scsw_tm_is_valid_ectl);

/**
 * scsw_tm_is_valid_pno - check pno field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the pno field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_pno(union scsw *scsw)
{
	return (scsw->tm.fctl != 0) &&
	       (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (!(scsw->tm.stctl & SCSW_STCTL_INTER_STATUS) ||
		 ((scsw->tm.stctl & SCSW_STCTL_INTER_STATUS) &&
		  (scsw->tm.actl & SCSW_ACTL_SUSPENDED)));
}
EXPORT_SYMBOL(scsw_tm_is_valid_pno);

/**
 * scsw_tm_is_valid_fctl - check fctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fctl field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_fctl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}
EXPORT_SYMBOL(scsw_tm_is_valid_fctl);

/**
 * scsw_tm_is_valid_actl - check actl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the actl field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_actl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}
EXPORT_SYMBOL(scsw_tm_is_valid_actl);

/**
 * scsw_tm_is_valid_stctl - check stctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the stctl field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_stctl(union scsw *scsw)
{
	/* Only valid if pmcw.dnv == 1*/
	return 1;
}
EXPORT_SYMBOL(scsw_tm_is_valid_stctl);

/**
 * scsw_tm_is_valid_dstat - check dstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the dstat field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_dstat(union scsw *scsw)
{
	return (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (scsw->tm.cc != 3);
}
EXPORT_SYMBOL(scsw_tm_is_valid_dstat);

/**
 * scsw_tm_is_valid_cstat - check cstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cstat field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_cstat(union scsw *scsw)
{
	return (scsw->tm.stctl & SCSW_STCTL_STATUS_PEND) &&
	       (scsw->tm.cc != 3);
}
EXPORT_SYMBOL(scsw_tm_is_valid_cstat);

/**
 * scsw_tm_is_valid_fcxs - check fcxs field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fcxs field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_fcxs(union scsw *scsw)
{
	return 1;
}
EXPORT_SYMBOL(scsw_tm_is_valid_fcxs);

/**
 * scsw_tm_is_valid_schxs - check schxs field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the schxs field of the specified transport mode scsw is
 * valid, zero otherwise.
 */
int scsw_tm_is_valid_schxs(union scsw *scsw)
{
	return (scsw->tm.cstat & (SCHN_STAT_PROG_CHECK |
				  SCHN_STAT_INTF_CTRL_CHK |
				  SCHN_STAT_PROT_CHECK |
				  SCHN_STAT_CHN_DATA_CHK));
}
EXPORT_SYMBOL(scsw_tm_is_valid_schxs);

/**
 * scsw_is_valid_actl - check actl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the actl field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_actl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_actl(scsw);
	else
		return scsw_cmd_is_valid_actl(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_actl);

/**
 * scsw_is_valid_cc - check cc field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cc field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_cc(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_cc(scsw);
	else
		return scsw_cmd_is_valid_cc(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_cc);

/**
 * scsw_is_valid_cstat - check cstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the cstat field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_cstat(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_cstat(scsw);
	else
		return scsw_cmd_is_valid_cstat(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_cstat);

/**
 * scsw_is_valid_dstat - check dstat field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the dstat field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_dstat(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_dstat(scsw);
	else
		return scsw_cmd_is_valid_dstat(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_dstat);

/**
 * scsw_is_valid_ectl - check ectl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the ectl field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_ectl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_ectl(scsw);
	else
		return scsw_cmd_is_valid_ectl(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_ectl);

/**
 * scsw_is_valid_eswf - check eswf field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the eswf field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_eswf(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_eswf(scsw);
	else
		return scsw_cmd_is_valid_eswf(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_eswf);

/**
 * scsw_is_valid_fctl - check fctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the fctl field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_fctl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_fctl(scsw);
	else
		return scsw_cmd_is_valid_fctl(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_fctl);

/**
 * scsw_is_valid_key - check key field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the key field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_key(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_key(scsw);
	else
		return scsw_cmd_is_valid_key(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_key);

/**
 * scsw_is_valid_pno - check pno field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the pno field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_pno(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_pno(scsw);
	else
		return scsw_cmd_is_valid_pno(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_pno);

/**
 * scsw_is_valid_stctl - check stctl field validity
 * @scsw: pointer to scsw
 *
 * Return non-zero if the stctl field of the specified scsw is valid,
 * regardless of whether it is a transport mode or command mode scsw.
 * Return zero if the field does not contain a valid value.
 */
int scsw_is_valid_stctl(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_valid_stctl(scsw);
	else
		return scsw_cmd_is_valid_stctl(scsw);
}
EXPORT_SYMBOL(scsw_is_valid_stctl);

/**
 * scsw_cmd_is_solicited - check for solicited scsw
 * @scsw: pointer to scsw
 *
 * Return non-zero if the command mode scsw indicates that the associated
 * status condition is solicited, zero if it is unsolicited.
 */
int scsw_cmd_is_solicited(union scsw *scsw)
{
	return (scsw->cmd.cc != 0) || (scsw->cmd.stctl !=
		(SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS));
}
EXPORT_SYMBOL(scsw_cmd_is_solicited);

/**
 * scsw_tm_is_solicited - check for solicited scsw
 * @scsw: pointer to scsw
 *
 * Return non-zero if the transport mode scsw indicates that the associated
 * status condition is solicited, zero if it is unsolicited.
 */
int scsw_tm_is_solicited(union scsw *scsw)
{
	return (scsw->tm.cc != 0) || (scsw->tm.stctl !=
		(SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS));
}
EXPORT_SYMBOL(scsw_tm_is_solicited);

/**
 * scsw_is_solicited - check for solicited scsw
 * @scsw: pointer to scsw
 *
 * Return non-zero if the transport or command mode scsw indicates that the
 * associated status condition is solicited, zero if it is unsolicited.
 */
int scsw_is_solicited(union scsw *scsw)
{
	if (scsw_is_tm(scsw))
		return scsw_tm_is_solicited(scsw);
	else
		return scsw_cmd_is_solicited(scsw);
}
EXPORT_SYMBOL(scsw_is_solicited);
