// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skfddi.c" for further information.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"
#include "h/smt_p.h"
#include <linux/bitrev.h>
#include <linux/kernel.h>

#define KERNEL
#include "h/smtstate.h"

#ifndef	lint
static const char ID_sccs[] = "@(#)smt.c	2.43 98/11/23 (C) SK " ;
#endif

/*
 * FC in SMbuf
 */
#define m_fc(mb)	((mb)->sm_data[0])

#define SMT_TID_MAGIC	0x1f0a7b3c

static const char *const smt_type_name[] = {
	"SMT_00??", "SMT_INFO", "SMT_02??", "SMT_03??",
	"SMT_04??", "SMT_05??", "SMT_06??", "SMT_07??",
	"SMT_08??", "SMT_09??", "SMT_0A??", "SMT_0B??",
	"SMT_0C??", "SMT_0D??", "SMT_0E??", "SMT_NSA"
} ;

static const char *const smt_class_name[] = {
	"UNKNOWN","NIF","SIF_CONFIG","SIF_OPER","ECF","RAF","RDF",
	"SRF","PMF_GET","PMF_SET","ESF"
} ;

#define LAST_CLASS	(SMT_PMF_SET)

static const struct fddi_addr SMT_Unknown = {
	{ 0,0,0x1f,0,0,0 }
} ;

/*
 * function prototypes
 */
#ifdef	LITTLE_ENDIAN
static int smt_swap_short(u_short s);
#endif
static int mac_index(struct s_smc *smc, int mac);
static int phy_index(struct s_smc *smc, int phy);
static int mac_con_resource_index(struct s_smc *smc, int mac);
static int phy_con_resource_index(struct s_smc *smc, int phy);
static void smt_send_rdf(struct s_smc *smc, SMbuf *rej, int fc, int reason,
			 int local);
static void smt_send_nif(struct s_smc *smc, const struct fddi_addr *dest, 
			 int fc, u_long tid, int type, int local);
static void smt_send_ecf(struct s_smc *smc, struct fddi_addr *dest, int fc,
                         u_long tid, int type, int len);
static void smt_echo_test(struct s_smc *smc, int dna);
static void smt_send_sif_config(struct s_smc *smc, struct fddi_addr *dest,
				u_long tid, int local);
static void smt_send_sif_operation(struct s_smc *smc, struct fddi_addr *dest,
				   u_long tid, int local);
#ifdef LITTLE_ENDIAN
static void smt_string_swap(char *data, const char *format, int len);
#endif
static void smt_add_frame_len(SMbuf *mb, int len);
static void smt_fill_una(struct s_smc *smc, struct smt_p_una *una);
static void smt_fill_sde(struct s_smc *smc, struct smt_p_sde *sde);
static void smt_fill_state(struct s_smc *smc, struct smt_p_state *state);
static void smt_fill_timestamp(struct s_smc *smc, struct smt_p_timestamp *ts);
static void smt_fill_policy(struct s_smc *smc, struct smt_p_policy *policy);
static void smt_fill_latency(struct s_smc *smc, struct smt_p_latency *latency);
static void smt_fill_neighbor(struct s_smc *smc, struct smt_p_neighbor *neighbor);
static int smt_fill_path(struct s_smc *smc, struct smt_p_path *path);
static void smt_fill_mac_status(struct s_smc *smc, struct smt_p_mac_status *st);
static void smt_fill_lem(struct s_smc *smc, struct smt_p_lem *lem, int phy);
static void smt_fill_version(struct s_smc *smc, struct smt_p_version *vers);
static void smt_fill_fsc(struct s_smc *smc, struct smt_p_fsc *fsc);
static void smt_fill_mac_counter(struct s_smc *smc, struct smt_p_mac_counter *mc);
static void smt_fill_mac_fnc(struct s_smc *smc, struct smt_p_mac_fnc *fnc);
static void smt_fill_manufacturer(struct s_smc *smc, 
				  struct smp_p_manufacturer *man);
static void smt_fill_user(struct s_smc *smc, struct smp_p_user *user);
static void smt_fill_setcount(struct s_smc *smc, struct smt_p_setcount *setcount);
static void smt_fill_echo(struct s_smc *smc, struct smt_p_echo *echo, u_long seed,
			  int len);

static void smt_clear_una_dna(struct s_smc *smc);
static void smt_clear_old_una_dna(struct s_smc *smc);
#ifdef	CONCENTRATOR
static int entity_to_index(void);
#endif
static void update_dac(struct s_smc *smc, int report);
static int div_ratio(u_long upper, u_long lower);
#ifdef  USE_CAN_ADDR
static void	hwm_conv_can(struct s_smc *smc, char *data, int len);
#else
#define		hwm_conv_can(smc,data,len)
#endif


static inline int is_my_addr(const struct s_smc *smc, 
			     const struct fddi_addr *addr)
{
	return(*(short *)(&addr->a[0]) ==
		*(short *)(&smc->mib.m[MAC0].fddiMACSMTAddress.a[0])
	  && *(short *)(&addr->a[2]) ==
		*(short *)(&smc->mib.m[MAC0].fddiMACSMTAddress.a[2])
	  && *(short *)(&addr->a[4]) ==
		*(short *)(&smc->mib.m[MAC0].fddiMACSMTAddress.a[4])) ;
}

static inline int is_broadcast(const struct fddi_addr *addr)
{
	return *(u_short *)(&addr->a[0]) == 0xffff &&
	       *(u_short *)(&addr->a[2]) == 0xffff &&
	       *(u_short *)(&addr->a[4]) == 0xffff;
}

static inline int is_individual(const struct fddi_addr *addr)
{
	return !(addr->a[0] & GROUP_ADDR);
}

static inline int is_equal(const struct fddi_addr *addr1, 
			   const struct fddi_addr *addr2)
{
	return *(u_short *)(&addr1->a[0]) == *(u_short *)(&addr2->a[0]) &&
	       *(u_short *)(&addr1->a[2]) == *(u_short *)(&addr2->a[2]) &&
	       *(u_short *)(&addr1->a[4]) == *(u_short *)(&addr2->a[4]);
}

/*
 * list of mandatory paras in frames
 */
static const u_short plist_nif[] = { SMT_P_UNA,SMT_P_SDE,SMT_P_STATE,0 } ;

/*
 * init SMT agent
 */
void smt_agent_init(struct s_smc *smc)
{
	int		i ;

	/*
	 * get MAC address
	 */
	smc->mib.m[MAC0].fddiMACSMTAddress = smc->hw.fddi_home_addr ;

	/*
	 * get OUI address from driver (bia == built-in-address)
	 */
	smc->mib.fddiSMTStationId.sid_oem[0] = 0 ;
	smc->mib.fddiSMTStationId.sid_oem[1] = 0 ;
	driver_get_bia(smc,&smc->mib.fddiSMTStationId.sid_node) ;
	for (i = 0 ; i < 6 ; i ++) {
		smc->mib.fddiSMTStationId.sid_node.a[i] =
			bitrev8(smc->mib.fddiSMTStationId.sid_node.a[i]);
	}
	smc->mib.fddiSMTManufacturerData[0] =
		smc->mib.fddiSMTStationId.sid_node.a[0] ;
	smc->mib.fddiSMTManufacturerData[1] =
		smc->mib.fddiSMTStationId.sid_node.a[1] ;
	smc->mib.fddiSMTManufacturerData[2] =
		smc->mib.fddiSMTStationId.sid_node.a[2] ;
	smc->sm.smt_tid = 0 ;
	smc->mib.m[MAC0].fddiMACDupAddressTest = DA_NONE ;
	smc->mib.m[MAC0].fddiMACUNDA_Flag = FALSE ;
#ifndef	SLIM_SMT
	smt_clear_una_dna(smc) ;
	smt_clear_old_una_dna(smc) ;
#endif
	for (i = 0 ; i < SMT_MAX_TEST ; i++)
		smc->sm.pend[i] = 0 ;
	smc->sm.please_reconnect = 0 ;
	smc->sm.uniq_ticks = 0 ;
}

/*
 * SMT task
 * forever
 *	delay 30 seconds
 *	send NIF
 *	check tvu & tvd
 * end
 */
void smt_agent_task(struct s_smc *smc)
{
	smt_timer_start(smc,&smc->sm.smt_timer, (u_long)1000000L,
		EV_TOKEN(EVENT_SMT,SM_TIMER)) ;
	DB_SMT("SMT agent task");
}

#ifndef SMT_REAL_TOKEN_CT
void smt_emulate_token_ct(struct s_smc *smc, int mac_index)
{
	u_long	count;
	u_long	time;


	time = smt_get_time();
	count =	((time - smc->sm.last_tok_time[mac_index]) *
					100)/TICKS_PER_SECOND;

	/*
	 * Only when ring is up we will have a token count. The
	 * flag is unfortunately a single instance value. This
	 * doesn't matter now, because we currently have only
	 * one MAC instance.
	 */
	if (smc->hw.mac_ring_is_up){
		smc->mib.m[mac_index].fddiMACToken_Ct += count;
	}

	/* Remember current time */
	smc->sm.last_tok_time[mac_index] = time;

}
#endif

/*ARGSUSED1*/
void smt_event(struct s_smc *smc, int event)
{
	u_long		time ;
#ifndef SMT_REAL_TOKEN_CT
	int		i ;
#endif


	if (smc->sm.please_reconnect) {
		smc->sm.please_reconnect -- ;
		if (smc->sm.please_reconnect == 0) {
			/* Counted down */
			queue_event(smc,EVENT_ECM,EC_CONNECT) ;
		}
	}

	if (event == SM_FAST)
		return ;

	/*
	 * timer for periodic cleanup in driver
	 * reset and start the watchdog (FM2)
	 * ESS timer
	 * SBA timer
	 */
	smt_timer_poll(smc) ;
	smt_start_watchdog(smc) ;
#ifndef	SLIM_SMT
#ifndef BOOT
#ifdef	ESS
	ess_timer_poll(smc) ;
#endif
#endif
#ifdef	SBA
	sba_timer_poll(smc) ;
#endif

	smt_srf_event(smc,0,0,0) ;

#endif	/* no SLIM_SMT */

	time = smt_get_time() ;

	if (time - smc->sm.smt_last_lem >= TICKS_PER_SECOND*8) {
		/*
		 * Use 8 sec. for the time intervall, it simplifies the
		 * LER estimation.
		 */
		struct fddi_mib_m	*mib ;
		u_long			upper ;
		u_long			lower ;
		int			cond ;
		int			port;
		struct s_phy		*phy ;
		/*
		 * calculate LEM bit error rate
		 */
		sm_lem_evaluate(smc) ;
		smc->sm.smt_last_lem = time ;

		/*
		 * check conditions
		 */
#ifndef	SLIM_SMT
		mac_update_counter(smc) ;
		mib = smc->mib.m ;
		upper =
		(mib->fddiMACLost_Ct - mib->fddiMACOld_Lost_Ct) +
		(mib->fddiMACError_Ct - mib->fddiMACOld_Error_Ct) ;
		lower =
		(mib->fddiMACFrame_Ct - mib->fddiMACOld_Frame_Ct) +
		(mib->fddiMACLost_Ct - mib->fddiMACOld_Lost_Ct) ;
		mib->fddiMACFrameErrorRatio = div_ratio(upper,lower) ;

		cond =
			((!mib->fddiMACFrameErrorThreshold &&
			mib->fddiMACError_Ct != mib->fddiMACOld_Error_Ct) ||
			(mib->fddiMACFrameErrorRatio >
			mib->fddiMACFrameErrorThreshold)) ;

		if (cond != mib->fddiMACFrameErrorFlag)
			smt_srf_event(smc,SMT_COND_MAC_FRAME_ERROR,
				INDEX_MAC,cond) ;

		upper =
		(mib->fddiMACNotCopied_Ct - mib->fddiMACOld_NotCopied_Ct) ;
		lower =
		upper +
		(mib->fddiMACCopied_Ct - mib->fddiMACOld_Copied_Ct) ;
		mib->fddiMACNotCopiedRatio = div_ratio(upper,lower) ;

		cond =
			((!mib->fddiMACNotCopiedThreshold &&
			mib->fddiMACNotCopied_Ct !=
				mib->fddiMACOld_NotCopied_Ct)||
			(mib->fddiMACNotCopiedRatio >
			mib->fddiMACNotCopiedThreshold)) ;

		if (cond != mib->fddiMACNotCopiedFlag)
			smt_srf_event(smc,SMT_COND_MAC_NOT_COPIED,
				INDEX_MAC,cond) ;

		/*
		 * set old values
		 */
		mib->fddiMACOld_Frame_Ct = mib->fddiMACFrame_Ct ;
		mib->fddiMACOld_Copied_Ct = mib->fddiMACCopied_Ct ;
		mib->fddiMACOld_Error_Ct = mib->fddiMACError_Ct ;
		mib->fddiMACOld_Lost_Ct = mib->fddiMACLost_Ct ;
		mib->fddiMACOld_NotCopied_Ct = mib->fddiMACNotCopied_Ct ;

		/*
		 * Check port EBError Condition
		 */
		for (port = 0; port < NUMPHYS; port ++) {
			phy = &smc->y[port] ;

			if (!phy->mib->fddiPORTHardwarePresent) {
				continue;
			}

			cond = (phy->mib->fddiPORTEBError_Ct -
				phy->mib->fddiPORTOldEBError_Ct > 5) ;

			/* If ratio is more than 5 in 8 seconds
			 * Set the condition.
			 */
			smt_srf_event(smc,SMT_COND_PORT_EB_ERROR,
				(int) (INDEX_PORT+ phy->np) ,cond) ;

			/*
			 * set old values
			 */
			phy->mib->fddiPORTOldEBError_Ct =
				phy->mib->fddiPORTEBError_Ct ;
		}

#endif	/* no SLIM_SMT */
	}

#ifndef	SLIM_SMT

	if (time - smc->sm.smt_last_notify >= (u_long)
		(smc->mib.fddiSMTTT_Notify * TICKS_PER_SECOND) ) {
		/*
		 * we can either send an announcement or a request
		 * a request will trigger a reply so that we can update
		 * our dna
		 * note: same tid must be used until reply is received
		 */
		if (!smc->sm.pend[SMT_TID_NIF])
			smc->sm.pend[SMT_TID_NIF] = smt_get_tid(smc) ;
		smt_send_nif(smc,&fddi_broadcast, FC_SMT_NSA,
			smc->sm.pend[SMT_TID_NIF], SMT_REQUEST,0) ;
		smc->sm.smt_last_notify = time ;
	}

	/*
	 * check timer
	 */
	if (smc->sm.smt_tvu &&
	    time - smc->sm.smt_tvu > 228*TICKS_PER_SECOND) {
		DB_SMT("SMT : UNA expired");
		smc->sm.smt_tvu = 0 ;

		if (!is_equal(&smc->mib.m[MAC0].fddiMACUpstreamNbr,
			&SMT_Unknown)){
			/* Do not update unknown address */
			smc->mib.m[MAC0].fddiMACOldUpstreamNbr=
				smc->mib.m[MAC0].fddiMACUpstreamNbr ;
		}
		smc->mib.m[MAC0].fddiMACUpstreamNbr = SMT_Unknown ;
		smc->mib.m[MAC0].fddiMACUNDA_Flag = FALSE ;
		/*
		 * Make sure the fddiMACUNDA_Flag = FALSE is
		 * included in the SRF so we don't generate
		 * a separate SRF for the deassertion of this
		 * condition
		 */
		update_dac(smc,0) ;
		smt_srf_event(smc, SMT_EVENT_MAC_NEIGHBOR_CHANGE,
			INDEX_MAC,0) ;
	}
	if (smc->sm.smt_tvd &&
	    time - smc->sm.smt_tvd > 228*TICKS_PER_SECOND) {
		DB_SMT("SMT : DNA expired");
		smc->sm.smt_tvd = 0 ;
		if (!is_equal(&smc->mib.m[MAC0].fddiMACDownstreamNbr,
			&SMT_Unknown)){
			/* Do not update unknown address */
			smc->mib.m[MAC0].fddiMACOldDownstreamNbr=
				smc->mib.m[MAC0].fddiMACDownstreamNbr ;
		}
		smc->mib.m[MAC0].fddiMACDownstreamNbr = SMT_Unknown ;
		smt_srf_event(smc, SMT_EVENT_MAC_NEIGHBOR_CHANGE,
			INDEX_MAC,0) ;
	}

#endif	/* no SLIM_SMT */

#ifndef SMT_REAL_TOKEN_CT
	/*
	 * Token counter emulation section. If hardware supports the token
	 * count, the token counter will be updated in mac_update_counter.
	 */
	for (i = MAC0; i < NUMMACS; i++ ){
		if (time - smc->sm.last_tok_time[i] > 2*TICKS_PER_SECOND ){
			smt_emulate_token_ct( smc, i );
		}
	}
#endif

	smt_timer_start(smc,&smc->sm.smt_timer, (u_long)1000000L,
		EV_TOKEN(EVENT_SMT,SM_TIMER)) ;
}

static int div_ratio(u_long upper, u_long lower)
{
	if ((upper<<16L) < upper)
		upper = 0xffff0000L ;
	else
		upper <<= 16L ;
	if (!lower)
		return 0;
	return (int)(upper/lower) ;
}

#ifndef	SLIM_SMT

/*
 * receive packet handler
 */
void smt_received_pack(struct s_smc *smc, SMbuf *mb, int fs)
/* int fs;  frame status */
{
	struct smt_header	*sm ;
	int			local ;

	int			illegal = 0 ;

	switch (m_fc(mb)) {
	case FC_SMT_INFO :
	case FC_SMT_LAN_LOC :
	case FC_SMT_LOC :
	case FC_SMT_NSA :
		break ;
	default :
		smt_free_mbuf(smc,mb) ;
		return ;
	}

	smc->mib.m[MAC0].fddiMACSMTCopied_Ct++ ;
	sm = smtod(mb,struct smt_header *) ;
	local = ((fs & L_INDICATOR) != 0) ;
	hwm_conv_can(smc,(char *)sm,12) ;

	/* check destination address */
	if (is_individual(&sm->smt_dest) && !is_my_addr(smc,&sm->smt_dest)) {
		smt_free_mbuf(smc,mb) ;
		return ;
	}
#if	0		/* for DUP recognition, do NOT filter them */
	/* ignore loop back packets */
	if (is_my_addr(smc,&sm->smt_source) && !local) {
		smt_free_mbuf(smc,mb) ;
		return ;
	}
#endif

	smt_swap_para(sm,(int) mb->sm_len,1) ;
	DB_SMT("SMT : received packet [%s] at 0x%p",
	       smt_type_name[m_fc(mb) & 0xf], sm);
	DB_SMT("SMT : version %d, class %s",
	       sm->smt_version,
	       smt_class_name[sm->smt_class > LAST_CLASS ? 0 : sm->smt_class]);

#ifdef	SBA
	/*
	 * check if NSA frame
	 */
	if (m_fc(mb) == FC_SMT_NSA && sm->smt_class == SMT_NIF &&
		(sm->smt_type == SMT_ANNOUNCE || sm->smt_type == SMT_REQUEST)) {
			smc->sba.sm = sm ;
			sba(smc,NIF) ;
	}
#endif

	/*
	 * ignore any packet with NSA and A-indicator set
	 */
	if ( (fs & A_INDICATOR) && m_fc(mb) == FC_SMT_NSA) {
		DB_SMT("SMT : ignoring NSA with A-indicator set from %s",
		       addr_to_string(&sm->smt_source));
		smt_free_mbuf(smc,mb) ;
		return ;
	}

	/*
	 * ignore frames with illegal length
	 */
	if (((sm->smt_class == SMT_ECF) && (sm->smt_len > SMT_MAX_ECHO_LEN)) ||
	    ((sm->smt_class != SMT_ECF) && (sm->smt_len > SMT_MAX_INFO_LEN))) {
		smt_free_mbuf(smc,mb) ;
		return ;
	}

	/*
	 * check SMT version
	 */
	switch (sm->smt_class) {
	case SMT_NIF :
	case SMT_SIF_CONFIG :
	case SMT_SIF_OPER :
	case SMT_ECF :
		if (sm->smt_version != SMT_VID)
			illegal = 1;
		break ;
	default :
		if (sm->smt_version != SMT_VID_2)
			illegal = 1;
		break ;
	}
	if (illegal) {
		DB_SMT("SMT : version = %d, dest = %s",
		       sm->smt_version, addr_to_string(&sm->smt_source));
		smt_send_rdf(smc,mb,m_fc(mb),SMT_RDF_VERSION,local) ;
		smt_free_mbuf(smc,mb) ;
		return ;
	}
	if ((sm->smt_len > mb->sm_len - sizeof(struct smt_header)) ||
	    ((sm->smt_len & 3) && (sm->smt_class != SMT_ECF))) {
		DB_SMT("SMT: info length error, len = %d", sm->smt_len);
		smt_send_rdf(smc,mb,m_fc(mb),SMT_RDF_LENGTH,local) ;
		smt_free_mbuf(smc,mb) ;
		return ;
	}
	switch (sm->smt_class) {
	case SMT_NIF :
		if (smt_check_para(smc,sm,plist_nif)) {
			DB_SMT("SMT: NIF with para problem, ignoring");
			break ;
		}
		switch (sm->smt_type) {
		case SMT_ANNOUNCE :
		case SMT_REQUEST :
			if (!(fs & C_INDICATOR) && m_fc(mb) == FC_SMT_NSA
				&& is_broadcast(&sm->smt_dest)) {
				struct smt_p_state	*st ;

				/* set my UNA */
				if (!is_equal(
					&smc->mib.m[MAC0].fddiMACUpstreamNbr,
					&sm->smt_source)) {
					DB_SMT("SMT : updated my UNA = %s",
					       addr_to_string(&sm->smt_source));
					if (!is_equal(&smc->mib.m[MAC0].
					    fddiMACUpstreamNbr,&SMT_Unknown)){
					 /* Do not update unknown address */
					 smc->mib.m[MAC0].fddiMACOldUpstreamNbr=
					 smc->mib.m[MAC0].fddiMACUpstreamNbr ;
					}

					smc->mib.m[MAC0].fddiMACUpstreamNbr =
						sm->smt_source ;
					smt_srf_event(smc,
						SMT_EVENT_MAC_NEIGHBOR_CHANGE,
						INDEX_MAC,0) ;
					smt_echo_test(smc,0) ;
				}
				smc->sm.smt_tvu = smt_get_time() ;
				st = (struct smt_p_state *)
					sm_to_para(smc,sm,SMT_P_STATE) ;
				if (st) {
					smc->mib.m[MAC0].fddiMACUNDA_Flag =
					(st->st_dupl_addr & SMT_ST_MY_DUPA) ?
					TRUE : FALSE ;
					update_dac(smc,1) ;
				}
			}
			if ((sm->smt_type == SMT_REQUEST) &&
			    is_individual(&sm->smt_source) &&
			    ((!(fs & A_INDICATOR) && m_fc(mb) == FC_SMT_NSA) ||
			     (m_fc(mb) != FC_SMT_NSA))) {
				DB_SMT("SMT : replying to NIF request %s",
				       addr_to_string(&sm->smt_source));
				smt_send_nif(smc,&sm->smt_source,
					FC_SMT_INFO,
					sm->smt_tid,
					SMT_REPLY,local) ;
			}
			break ;
		case SMT_REPLY :
			DB_SMT("SMT : received NIF response from %s",
			       addr_to_string(&sm->smt_source));
			if (fs & A_INDICATOR) {
				smc->sm.pend[SMT_TID_NIF] = 0 ;
				DB_SMT("SMT : duplicate address");
				smc->mib.m[MAC0].fddiMACDupAddressTest =
					DA_FAILED ;
				smc->r.dup_addr_test = DA_FAILED ;
				queue_event(smc,EVENT_RMT,RM_DUP_ADDR) ;
				smc->mib.m[MAC0].fddiMACDA_Flag = TRUE ;
				update_dac(smc,1) ;
				break ;
			}
			if (sm->smt_tid == smc->sm.pend[SMT_TID_NIF]) {
				smc->sm.pend[SMT_TID_NIF] = 0 ;
				/* set my DNA */
				if (!is_equal(
					&smc->mib.m[MAC0].fddiMACDownstreamNbr,
					&sm->smt_source)) {
					DB_SMT("SMT : updated my DNA");
					if (!is_equal(&smc->mib.m[MAC0].
					 fddiMACDownstreamNbr, &SMT_Unknown)){
					 /* Do not update unknown address */
				smc->mib.m[MAC0].fddiMACOldDownstreamNbr =
					 smc->mib.m[MAC0].fddiMACDownstreamNbr ;
					}

					smc->mib.m[MAC0].fddiMACDownstreamNbr =
						sm->smt_source ;
					smt_srf_event(smc,
						SMT_EVENT_MAC_NEIGHBOR_CHANGE,
						INDEX_MAC,0) ;
					smt_echo_test(smc,1) ;
				}
				smc->mib.m[MAC0].fddiMACDA_Flag = FALSE ;
				update_dac(smc,1) ;
				smc->sm.smt_tvd = smt_get_time() ;
				smc->mib.m[MAC0].fddiMACDupAddressTest =
					DA_PASSED ;
				if (smc->r.dup_addr_test != DA_PASSED) {
					smc->r.dup_addr_test = DA_PASSED ;
					queue_event(smc,EVENT_RMT,RM_DUP_ADDR) ;
				}
			}
			else if (sm->smt_tid ==
				smc->sm.pend[SMT_TID_NIF_TEST]) {
				DB_SMT("SMT : NIF test TID ok");
			}
			else {
				DB_SMT("SMT : expected TID %lx, got %x",
				       smc->sm.pend[SMT_TID_NIF], sm->smt_tid);
			}
			break ;
		default :
			illegal = 2 ;
			break ;
		}
		break ;
	case SMT_SIF_CONFIG :	/* station information */
		if (sm->smt_type != SMT_REQUEST)
			break ;
		DB_SMT("SMT : replying to SIF Config request from %s",
		       addr_to_string(&sm->smt_source));
		smt_send_sif_config(smc,&sm->smt_source,sm->smt_tid,local) ;
		break ;
	case SMT_SIF_OPER :	/* station information */
		if (sm->smt_type != SMT_REQUEST)
			break ;
		DB_SMT("SMT : replying to SIF Operation request from %s",
		       addr_to_string(&sm->smt_source));
		smt_send_sif_operation(smc,&sm->smt_source,sm->smt_tid,local) ;
		break ;
	case SMT_ECF :		/* echo frame */
		switch (sm->smt_type) {
		case SMT_REPLY :
			smc->mib.priv.fddiPRIVECF_Reply_Rx++ ;
			DB_SMT("SMT: received ECF reply from %s",
			       addr_to_string(&sm->smt_source));
			if (sm_to_para(smc,sm,SMT_P_ECHODATA) == NULL) {
				DB_SMT("SMT: ECHODATA missing");
				break ;
			}
			if (sm->smt_tid == smc->sm.pend[SMT_TID_ECF]) {
				DB_SMT("SMT : ECF test TID ok");
			}
			else if (sm->smt_tid == smc->sm.pend[SMT_TID_ECF_UNA]) {
				DB_SMT("SMT : ECF test UNA ok");
			}
			else if (sm->smt_tid == smc->sm.pend[SMT_TID_ECF_DNA]) {
				DB_SMT("SMT : ECF test DNA ok");
			}
			else {
				DB_SMT("SMT : expected TID %lx, got %x",
				       smc->sm.pend[SMT_TID_ECF],
				       sm->smt_tid);
			}
			break ;
		case SMT_REQUEST :
			smc->mib.priv.fddiPRIVECF_Req_Rx++ ;
			{
			if (sm->smt_len && !sm_to_para(smc,sm,SMT_P_ECHODATA)) {
				DB_SMT("SMT: ECF with para problem,sending RDF");
				smt_send_rdf(smc,mb,m_fc(mb),SMT_RDF_LENGTH,
					local) ;
				break ;
			}
			DB_SMT("SMT - sending ECF reply to %s",
			       addr_to_string(&sm->smt_source));

			/* set destination addr.  & reply */
			sm->smt_dest = sm->smt_source ;
			sm->smt_type = SMT_REPLY ;
			dump_smt(smc,sm,"ECF REPLY") ;
			smc->mib.priv.fddiPRIVECF_Reply_Tx++ ;
			smt_send_frame(smc,mb,FC_SMT_INFO,local) ;
			return ;		/* DON'T free mbuf */
			}
		default :
			illegal = 1 ;
			break ;
		}
		break ;
#ifndef	BOOT
	case SMT_RAF :		/* resource allocation */
#ifdef	ESS
		DB_ESSN(2, "ESS: RAF frame received");
		fs = ess_raf_received_pack(smc,mb,sm,fs) ;
#endif

#ifdef	SBA
		DB_SBAN(2,"SBA: RAF frame received\n",0,0) ;
		sba_raf_received_pack(smc,sm,fs) ;
#endif
		break ;
	case SMT_RDF :		/* request denied */
		smc->mib.priv.fddiPRIVRDF_Rx++ ;
		break ;
	case SMT_ESF :		/* extended service - not supported */
		if (sm->smt_type == SMT_REQUEST) {
			DB_SMT("SMT - received ESF, sending RDF");
			smt_send_rdf(smc,mb,m_fc(mb),SMT_RDF_CLASS,local) ;
		}
		break ;
	case SMT_PMF_GET :
	case SMT_PMF_SET :
		if (sm->smt_type != SMT_REQUEST)
			break ;
		/* update statistics */
		if (sm->smt_class == SMT_PMF_GET)
			smc->mib.priv.fddiPRIVPMF_Get_Rx++ ;
		else
			smc->mib.priv.fddiPRIVPMF_Set_Rx++ ;
		/*
		 * ignore PMF SET with I/G set
		 */
		if ((sm->smt_class == SMT_PMF_SET) &&
			!is_individual(&sm->smt_dest)) {
			DB_SMT("SMT: ignoring PMF-SET with I/G set");
			break ;
		}
		smt_pmf_received_pack(smc,mb, local) ;
		break ;
	case SMT_SRF :
		dump_smt(smc,sm,"SRF received") ;
		break ;
	default :
		if (sm->smt_type != SMT_REQUEST)
			break ;
		/*
		 * For frames with unknown class:
		 * we need to send a RDF frame according to 8.1.3.1.1,
		 * only if it is a REQUEST.
		 */
		DB_SMT("SMT : class = %d, send RDF to %s",
		       sm->smt_class, addr_to_string(&sm->smt_source));

		smt_send_rdf(smc,mb,m_fc(mb),SMT_RDF_CLASS,local) ;
		break ;
#endif
	}
	if (illegal) {
		DB_SMT("SMT: discarding invalid frame, reason = %d", illegal);
	}
	smt_free_mbuf(smc,mb) ;
}

static void update_dac(struct s_smc *smc, int report)
{
	int	cond ;

	cond = ( smc->mib.m[MAC0].fddiMACUNDA_Flag |
		smc->mib.m[MAC0].fddiMACDA_Flag) != 0 ;
	if (report && (cond != smc->mib.m[MAC0].fddiMACDuplicateAddressCond))
		smt_srf_event(smc, SMT_COND_MAC_DUP_ADDR,INDEX_MAC,cond) ;
	else
		smc->mib.m[MAC0].fddiMACDuplicateAddressCond = cond ;
}

/*
 * send SMT frame
 *	set source address
 *	set station ID
 *	send frame
 */
void smt_send_frame(struct s_smc *smc, SMbuf *mb, int fc, int local)
/* SMbuf *mb;	buffer to send */
/* int fc;	FC value */
{
	struct smt_header	*sm ;

	if (!smc->r.sm_ma_avail && !local) {
		smt_free_mbuf(smc,mb) ;
		return ;
	}
	sm = smtod(mb,struct smt_header *) ;
	sm->smt_source = smc->mib.m[MAC0].fddiMACSMTAddress ;
	sm->smt_sid = smc->mib.fddiSMTStationId ;

	smt_swap_para(sm,(int) mb->sm_len,0) ;		/* swap para & header */
	hwm_conv_can(smc,(char *)sm,12) ;		/* convert SA and DA */
	smc->mib.m[MAC0].fddiMACSMTTransmit_Ct++ ;
	smt_send_mbuf(smc,mb,local ? FC_SMT_LOC : fc) ;
}

/*
 * generate and send RDF
 */
static void smt_send_rdf(struct s_smc *smc, SMbuf *rej, int fc, int reason,
			 int local)
/* SMbuf *rej;	mbuf of offending frame */
/* int fc;	FC of denied frame */
/* int reason;	reason code */
{
	SMbuf	*mb ;
	struct smt_header	*sm ;	/* header of offending frame */
	struct smt_rdf	*rdf ;
	int		len ;
	int		frame_len ;

	sm = smtod(rej,struct smt_header *) ;
	if (sm->smt_type != SMT_REQUEST)
		return ;

	DB_SMT("SMT: sending RDF to %s,reason = 0x%x",
	       addr_to_string(&sm->smt_source), reason);


	/*
	 * note: get framelength from MAC length, NOT from SMT header
	 * smt header length is included in sm_len
	 */
	frame_len = rej->sm_len ;

	if (!(mb=smt_build_frame(smc,SMT_RDF,SMT_REPLY,sizeof(struct smt_rdf))))
		return ;
	rdf = smtod(mb,struct smt_rdf *) ;
	rdf->smt.smt_tid = sm->smt_tid ;		/* use TID from sm */
	rdf->smt.smt_dest = sm->smt_source ;		/* set dest = source */

	/* set P12 */
	rdf->reason.para.p_type = SMT_P_REASON ;
	rdf->reason.para.p_len = sizeof(struct smt_p_reason) - PARA_LEN ;
	rdf->reason.rdf_reason = reason ;

	/* set P14 */
	rdf->version.para.p_type = SMT_P_VERSION ;
	rdf->version.para.p_len = sizeof(struct smt_p_version) - PARA_LEN ;
	rdf->version.v_pad = 0 ;
	rdf->version.v_n = 1 ;
	rdf->version.v_index = 1 ;
	rdf->version.v_version[0] = SMT_VID_2 ;
	rdf->version.v_pad2 = 0 ;

	/* set P13 */
	if ((unsigned int) frame_len <= SMT_MAX_INFO_LEN - sizeof(*rdf) +
		2*sizeof(struct smt_header))
		len = frame_len ;
	else
		len = SMT_MAX_INFO_LEN - sizeof(*rdf) +
			2*sizeof(struct smt_header) ;
	/* make length multiple of 4 */
	len &= ~3 ;
	rdf->refused.para.p_type = SMT_P_REFUSED ;
	/* length of para is smt_frame + ref_fc */
	rdf->refused.para.p_len = len + 4 ;
	rdf->refused.ref_fc = fc ;

	/* swap it back */
	smt_swap_para(sm,frame_len,0) ;

	memcpy((char *) &rdf->refused.ref_header,(char *) sm,len) ;

	len -= sizeof(struct smt_header) ;
	mb->sm_len += len ;
	rdf->smt.smt_len += len ;

	dump_smt(smc,(struct smt_header *)rdf,"RDF") ;
	smc->mib.priv.fddiPRIVRDF_Tx++ ;
	smt_send_frame(smc,mb,FC_SMT_INFO,local) ;
}

/*
 * generate and send NIF
 */
static void smt_send_nif(struct s_smc *smc, const struct fddi_addr *dest, 
			 int fc, u_long tid, int type, int local)
/* struct fddi_addr *dest;	dest address */
/* int fc;			frame control */
/* u_long tid;			transaction id */
/* int type;			frame type */
{
	struct smt_nif	*nif ;
	SMbuf		*mb ;

	if (!(mb = smt_build_frame(smc,SMT_NIF,type,sizeof(struct smt_nif))))
		return ;
	nif = smtod(mb, struct smt_nif *) ;
	smt_fill_una(smc,&nif->una) ;	/* set UNA */
	smt_fill_sde(smc,&nif->sde) ;	/* set station descriptor */
	smt_fill_state(smc,&nif->state) ;	/* set state information */
#ifdef	SMT6_10
	smt_fill_fsc(smc,&nif->fsc) ;	/* set frame status cap. */
#endif
	nif->smt.smt_dest = *dest ;	/* destination address */
	nif->smt.smt_tid = tid ;	/* transaction ID */
	dump_smt(smc,(struct smt_header *)nif,"NIF") ;
	smt_send_frame(smc,mb,fc,local) ;
}

#ifdef	DEBUG
/*
 * send NIF request (test purpose)
 */
static void smt_send_nif_request(struct s_smc *smc, struct fddi_addr *dest)
{
	smc->sm.pend[SMT_TID_NIF_TEST] = smt_get_tid(smc) ;
	smt_send_nif(smc,dest, FC_SMT_INFO, smc->sm.pend[SMT_TID_NIF_TEST],
		SMT_REQUEST,0) ;
}

/*
 * send ECF request (test purpose)
 */
static void smt_send_ecf_request(struct s_smc *smc, struct fddi_addr *dest,
				 int len)
{
	smc->sm.pend[SMT_TID_ECF] = smt_get_tid(smc) ;
	smt_send_ecf(smc,dest, FC_SMT_INFO, smc->sm.pend[SMT_TID_ECF],
		SMT_REQUEST,len) ;
}
#endif

/*
 * echo test
 */
static void smt_echo_test(struct s_smc *smc, int dna)
{
	u_long	tid ;

	smc->sm.pend[dna ? SMT_TID_ECF_DNA : SMT_TID_ECF_UNA] =
		tid = smt_get_tid(smc) ;
	smt_send_ecf(smc, dna ?
		&smc->mib.m[MAC0].fddiMACDownstreamNbr :
		&smc->mib.m[MAC0].fddiMACUpstreamNbr,
		FC_SMT_INFO,tid, SMT_REQUEST, (SMT_TEST_ECHO_LEN & ~3)-8) ;
}

/*
 * generate and send ECF
 */
static void smt_send_ecf(struct s_smc *smc, struct fddi_addr *dest, int fc,
			 u_long tid, int type, int len)
/* struct fddi_addr *dest;	dest address */
/* int fc;			frame control */
/* u_long tid;			transaction id */
/* int type;			frame type */
/* int len;			frame length */
{
	struct smt_ecf	*ecf ;
	SMbuf		*mb ;

	if (!(mb = smt_build_frame(smc,SMT_ECF,type,SMT_ECF_LEN + len)))
		return ;
	ecf = smtod(mb, struct smt_ecf *) ;

	smt_fill_echo(smc,&ecf->ec_echo,tid,len) ;	/* set ECHO */
	ecf->smt.smt_dest = *dest ;	/* destination address */
	ecf->smt.smt_tid = tid ;	/* transaction ID */
	smc->mib.priv.fddiPRIVECF_Req_Tx++ ;
	smt_send_frame(smc,mb,fc,0) ;
}

/*
 * generate and send SIF config response
 */

static void smt_send_sif_config(struct s_smc *smc, struct fddi_addr *dest,
				u_long tid, int local)
/* struct fddi_addr *dest;	dest address */
/* u_long tid;			transaction id */
{
	struct smt_sif_config	*sif ;
	SMbuf			*mb ;
	int			len ;
	if (!(mb = smt_build_frame(smc,SMT_SIF_CONFIG,SMT_REPLY,
		SIZEOF_SMT_SIF_CONFIG)))
		return ;

	sif = smtod(mb, struct smt_sif_config *) ;
	smt_fill_timestamp(smc,&sif->ts) ;	/* set time stamp */
	smt_fill_sde(smc,&sif->sde) ;		/* set station descriptor */
	smt_fill_version(smc,&sif->version) ;	/* set version information */
	smt_fill_state(smc,&sif->state) ;	/* set state information */
	smt_fill_policy(smc,&sif->policy) ;	/* set station policy */
	smt_fill_latency(smc,&sif->latency);	/* set station latency */
	smt_fill_neighbor(smc,&sif->neighbor);	/* set station neighbor */
	smt_fill_setcount(smc,&sif->setcount) ;	/* set count */
	len = smt_fill_path(smc,&sif->path);	/* set station path descriptor*/
	sif->smt.smt_dest = *dest ;		/* destination address */
	sif->smt.smt_tid = tid ;		/* transaction ID */
	smt_add_frame_len(mb,len) ;		/* adjust length fields */
	dump_smt(smc,(struct smt_header *)sif,"SIF Configuration Reply") ;
	smt_send_frame(smc,mb,FC_SMT_INFO,local) ;
}

/*
 * generate and send SIF operation response
 */

static void smt_send_sif_operation(struct s_smc *smc, struct fddi_addr *dest,
				   u_long tid, int local)
/* struct fddi_addr *dest;	dest address */
/* u_long tid;			transaction id */
{
	struct smt_sif_operation *sif ;
	SMbuf			*mb ;
	int			ports ;
	int			i ;

	ports = NUMPHYS ;
#ifndef	CONCENTRATOR
	if (smc->s.sas == SMT_SAS)
		ports = 1 ;
#endif

	if (!(mb = smt_build_frame(smc,SMT_SIF_OPER,SMT_REPLY,
		SIZEOF_SMT_SIF_OPERATION+ports*sizeof(struct smt_p_lem))))
		return ;
	sif = smtod(mb, struct smt_sif_operation *) ;
	smt_fill_timestamp(smc,&sif->ts) ;	/* set time stamp */
	smt_fill_mac_status(smc,&sif->status) ; /* set mac status */
	smt_fill_mac_counter(smc,&sif->mc) ; /* set mac counter field */
	smt_fill_mac_fnc(smc,&sif->fnc) ; /* set frame not copied counter */
	smt_fill_manufacturer(smc,&sif->man) ; /* set manufacturer field */
	smt_fill_user(smc,&sif->user) ;		/* set user field */
	smt_fill_setcount(smc,&sif->setcount) ;	/* set count */
	/*
	 * set link error mon information
	 */
	if (ports == 1) {
		smt_fill_lem(smc,sif->lem,PS) ;
	}
	else {
		for (i = 0 ; i < ports ; i++) {
			smt_fill_lem(smc,&sif->lem[i],i) ;
		}
	}

	sif->smt.smt_dest = *dest ;	/* destination address */
	sif->smt.smt_tid = tid ;	/* transaction ID */
	dump_smt(smc,(struct smt_header *)sif,"SIF Operation Reply") ;
	smt_send_frame(smc,mb,FC_SMT_INFO,local) ;
}

/*
 * get and initialize SMT frame
 */
SMbuf *smt_build_frame(struct s_smc *smc, int class, int type,
				  int length)
{
	SMbuf			*mb ;
	struct smt_header	*smt ;

#if	0
	if (!smc->r.sm_ma_avail) {
		return 0;
	}
#endif
	if (!(mb = smt_get_mbuf(smc)))
		return mb;

	mb->sm_len = length ;
	smt = smtod(mb, struct smt_header *) ;
	smt->smt_dest = fddi_broadcast ; /* set dest = broadcast */
	smt->smt_class = class ;
	smt->smt_type = type ;
	switch (class) {
	case SMT_NIF :
	case SMT_SIF_CONFIG :
	case SMT_SIF_OPER :
	case SMT_ECF :
		smt->smt_version = SMT_VID ;
		break ;
	default :
		smt->smt_version = SMT_VID_2 ;
		break ;
	}
	smt->smt_tid = smt_get_tid(smc) ;	/* set transaction ID */
	smt->smt_pad = 0 ;
	smt->smt_len = length - sizeof(struct smt_header) ;
	return mb;
}

static void smt_add_frame_len(SMbuf *mb, int len)
{
	struct smt_header	*smt ;

	smt = smtod(mb, struct smt_header *) ;
	smt->smt_len += len ;
	mb->sm_len += len ;
}



/*
 * fill values in UNA parameter
 */
static void smt_fill_una(struct s_smc *smc, struct smt_p_una *una)
{
	SMTSETPARA(una,SMT_P_UNA) ;
	una->una_pad = 0 ;
	una->una_node = smc->mib.m[MAC0].fddiMACUpstreamNbr ;
}

/*
 * fill values in SDE parameter
 */
static void smt_fill_sde(struct s_smc *smc, struct smt_p_sde *sde)
{
	SMTSETPARA(sde,SMT_P_SDE) ;
	sde->sde_non_master = smc->mib.fddiSMTNonMaster_Ct ;
	sde->sde_master = smc->mib.fddiSMTMaster_Ct ;
	sde->sde_mac_count = NUMMACS ;		/* only 1 MAC */
#ifdef	CONCENTRATOR
	sde->sde_type = SMT_SDE_CONCENTRATOR ;
#else
	sde->sde_type = SMT_SDE_STATION ;
#endif
}

/*
 * fill in values in station state parameter
 */
static void smt_fill_state(struct s_smc *smc, struct smt_p_state *state)
{
	int	top ;
	int	twist ;

	SMTSETPARA(state,SMT_P_STATE) ;
	state->st_pad = 0 ;

	/* determine topology */
	top = 0 ;
	if (smc->mib.fddiSMTPeerWrapFlag) {
		top |= SMT_ST_WRAPPED ;		/* state wrapped */
	}
#ifdef	CONCENTRATOR
	if (cfm_status_unattached(smc)) {
		top |= SMT_ST_UNATTACHED ;	/* unattached concentrator */
	}
#endif
	if ((twist = pcm_status_twisted(smc)) & 1) {
		top |= SMT_ST_TWISTED_A ;	/* twisted cable */
	}
	if (twist & 2) {
		top |= SMT_ST_TWISTED_B ;	/* twisted cable */
	}
#ifdef	OPT_SRF
	top |= SMT_ST_SRF ;
#endif
	if (pcm_rooted_station(smc))
		top |= SMT_ST_ROOTED_S ;
	if (smc->mib.a[0].fddiPATHSbaPayload != 0)
		top |= SMT_ST_SYNC_SERVICE ;
	state->st_topology = top ;
	state->st_dupl_addr =
		((smc->mib.m[MAC0].fddiMACDA_Flag ? SMT_ST_MY_DUPA : 0 ) |
		 (smc->mib.m[MAC0].fddiMACUNDA_Flag ? SMT_ST_UNA_DUPA : 0)) ;
}

/*
 * fill values in timestamp parameter
 */
static void smt_fill_timestamp(struct s_smc *smc, struct smt_p_timestamp *ts)
{

	SMTSETPARA(ts,SMT_P_TIMESTAMP) ;
	smt_set_timestamp(smc,ts->ts_time) ;
}

void smt_set_timestamp(struct s_smc *smc, u_char *p)
{
	u_long	time ;
	u_long	utime ;

	/*
	 * timestamp is 64 bits long ; resolution is 80 nS
	 * our clock resolution is 10mS
	 * 10mS/80ns = 125000 ~ 2^17 = 131072
	 */
	utime = smt_get_time() ;
	time = utime * 100 ;
	time /= TICKS_PER_SECOND ;
	p[0] = 0 ;
	p[1] = (u_char)((time>>(8+8+8+8-1)) & 1) ;
	p[2] = (u_char)(time>>(8+8+8-1)) ;
	p[3] = (u_char)(time>>(8+8-1)) ;
	p[4] = (u_char)(time>>(8-1)) ;
	p[5] = (u_char)(time<<1) ;
	p[6] = (u_char)(smc->sm.uniq_ticks>>8) ;
	p[7] = (u_char)smc->sm.uniq_ticks ;
	/*
	 * make sure we don't wrap: restart whenever the upper digits change
	 */
	if (utime != smc->sm.uniq_time) {
		smc->sm.uniq_ticks = 0 ;
	}
	smc->sm.uniq_ticks++ ;
	smc->sm.uniq_time = utime ;
}

/*
 * fill values in station policy parameter
 */
static void smt_fill_policy(struct s_smc *smc, struct smt_p_policy *policy)
{
	int	i ;
	const u_char *map ;
	u_short	in ;
	u_short	out ;

	/*
	 * MIB para 101b (fddiSMTConnectionPolicy) coding
	 * is different from 0005 coding
	 */
	static const u_char ansi_weirdness[16] = {
		0,7,5,3,8,1,6,4,9,10,2,11,12,13,14,15
	} ;
	SMTSETPARA(policy,SMT_P_POLICY) ;

	out = 0 ;
	in = smc->mib.fddiSMTConnectionPolicy ;
	for (i = 0, map = ansi_weirdness ; i < 16 ; i++) {
		if (in & 1)
			out |= (1<<*map) ;
		in >>= 1 ;
		map++ ;
	}
	policy->pl_config = smc->mib.fddiSMTConfigPolicy ;
	policy->pl_connect = out ;
}

/*
 * fill values in latency equivalent parameter
 */
static void smt_fill_latency(struct s_smc *smc, struct smt_p_latency *latency)
{
	SMTSETPARA(latency,SMT_P_LATENCY) ;

	latency->lt_phyout_idx1 = phy_index(smc,0) ;
	latency->lt_latency1 = 10 ;	/* in octets (byte clock) */
	/*
	 * note: latency has two phy entries by definition
	 * for a SAS, the 2nd one is null
	 */
	if (smc->s.sas == SMT_DAS) {
		latency->lt_phyout_idx2 = phy_index(smc,1) ;
		latency->lt_latency2 = 10 ;	/* in octets (byte clock) */
	}
	else {
		latency->lt_phyout_idx2 = 0 ;
		latency->lt_latency2 = 0 ;
	}
}

/*
 * fill values in MAC neighbors parameter
 */
static void smt_fill_neighbor(struct s_smc *smc, struct smt_p_neighbor *neighbor)
{
	SMTSETPARA(neighbor,SMT_P_NEIGHBORS) ;

	neighbor->nb_mib_index = INDEX_MAC ;
	neighbor->nb_mac_index = mac_index(smc,1) ;
	neighbor->nb_una = smc->mib.m[MAC0].fddiMACUpstreamNbr ;
	neighbor->nb_dna = smc->mib.m[MAC0].fddiMACDownstreamNbr ;
}

/*
 * fill values in path descriptor
 */
#ifdef	CONCENTRATOR
#define ALLPHYS	NUMPHYS
#else
#define ALLPHYS	((smc->s.sas == SMT_SAS) ? 1 : 2)
#endif

static int smt_fill_path(struct s_smc *smc, struct smt_p_path *path)
{
	SK_LOC_DECL(int,type) ;
	SK_LOC_DECL(int,state) ;
	SK_LOC_DECL(int,remote) ;
	SK_LOC_DECL(int,mac) ;
	int	len ;
	int	p ;
	int	physp ;
	struct smt_phy_rec	*phy ;
	struct smt_mac_rec	*pd_mac ;

	len =	PARA_LEN +
		sizeof(struct smt_mac_rec) * NUMMACS +
		sizeof(struct smt_phy_rec) * ALLPHYS ;
	path->para.p_type = SMT_P_PATH ;
	path->para.p_len = len - PARA_LEN ;

	/* PHYs */
	for (p = 0,phy = path->pd_phy ; p < ALLPHYS ; p++, phy++) {
		physp = p ;
#ifndef	CONCENTRATOR
		if (smc->s.sas == SMT_SAS)
			physp = PS ;
#endif
		pcm_status_state(smc,physp,&type,&state,&remote,&mac) ;
#ifdef	LITTLE_ENDIAN
		phy->phy_mib_index = smt_swap_short((u_short)p+INDEX_PORT) ;
#else
		phy->phy_mib_index = p+INDEX_PORT ;
#endif
		phy->phy_type = type ;
		phy->phy_connect_state = state ;
		phy->phy_remote_type = remote ;
		phy->phy_remote_mac = mac ;
		phy->phy_resource_idx = phy_con_resource_index(smc,p) ;
	}

	/* MAC */
	pd_mac = (struct smt_mac_rec *) phy ;
	pd_mac->mac_addr = smc->mib.m[MAC0].fddiMACSMTAddress ;
	pd_mac->mac_resource_idx = mac_con_resource_index(smc,1) ;
	return len;
}

/*
 * fill values in mac status
 */
static void smt_fill_mac_status(struct s_smc *smc, struct smt_p_mac_status *st)
{
	SMTSETPARA(st,SMT_P_MAC_STATUS) ;

	st->st_mib_index = INDEX_MAC ;
	st->st_mac_index = mac_index(smc,1) ;

	mac_update_counter(smc) ;
	/*
	 * timer values are represented in SMT as 2's complement numbers
	 * units :	internal :  2's complement BCLK
	 */
	st->st_t_req = smc->mib.m[MAC0].fddiMACT_Req ;
	st->st_t_neg = smc->mib.m[MAC0].fddiMACT_Neg ;
	st->st_t_max = smc->mib.m[MAC0].fddiMACT_Max ;
	st->st_tvx_value = smc->mib.m[MAC0].fddiMACTvxValue ;
	st->st_t_min = smc->mib.m[MAC0].fddiMACT_Min ;

	st->st_sba = smc->mib.a[PATH0].fddiPATHSbaPayload ;
	st->st_frame_ct = smc->mib.m[MAC0].fddiMACFrame_Ct ;
	st->st_error_ct = smc->mib.m[MAC0].fddiMACError_Ct ;
	st->st_lost_ct = smc->mib.m[MAC0].fddiMACLost_Ct ;
}

/*
 * fill values in LEM status
 */
static void smt_fill_lem(struct s_smc *smc, struct smt_p_lem *lem, int phy)
{
	struct fddi_mib_p	*mib ;

	mib = smc->y[phy].mib ;

	SMTSETPARA(lem,SMT_P_LEM) ;
	lem->lem_mib_index = phy+INDEX_PORT ;
	lem->lem_phy_index = phy_index(smc,phy) ;
	lem->lem_pad2 = 0 ;
	lem->lem_cutoff = mib->fddiPORTLer_Cutoff ;
	lem->lem_alarm = mib->fddiPORTLer_Alarm ;
	/* long term bit error rate */
	lem->lem_estimate = mib->fddiPORTLer_Estimate ;
	/* # of rejected connections */
	lem->lem_reject_ct = mib->fddiPORTLem_Reject_Ct ;
	lem->lem_ct = mib->fddiPORTLem_Ct ;	/* total number of errors */
}

/*
 * fill version parameter
 */
static void smt_fill_version(struct s_smc *smc, struct smt_p_version *vers)
{
	SK_UNUSED(smc) ;
	SMTSETPARA(vers,SMT_P_VERSION) ;
	vers->v_pad = 0 ;
	vers->v_n = 1 ;				/* one version is enough .. */
	vers->v_index = 1 ;
	vers->v_version[0] = SMT_VID_2 ;
	vers->v_pad2 = 0 ;
}

#ifdef	SMT6_10
/*
 * fill frame status capabilities
 */
/*
 * note: this para 200B is NOT in swap table, because it's also set in
 * PMF add_para
 */
static void smt_fill_fsc(struct s_smc *smc, struct smt_p_fsc *fsc)
{
	SK_UNUSED(smc) ;
	SMTSETPARA(fsc,SMT_P_FSC) ;
	fsc->fsc_pad0 = 0 ;
	fsc->fsc_mac_index = INDEX_MAC ;	/* this is MIB ; MIB is NOT
						 * mac_index ()i !
						 */
	fsc->fsc_pad1 = 0 ;
	fsc->fsc_value = FSC_TYPE0 ;		/* "normal" node */
#ifdef	LITTLE_ENDIAN
	fsc->fsc_mac_index = smt_swap_short(INDEX_MAC) ;
	fsc->fsc_value = smt_swap_short(FSC_TYPE0) ;
#endif
}
#endif

/*
 * fill mac counter field
 */
static void smt_fill_mac_counter(struct s_smc *smc, struct smt_p_mac_counter *mc)
{
	SMTSETPARA(mc,SMT_P_MAC_COUNTER) ;
	mc->mc_mib_index = INDEX_MAC ;
	mc->mc_index = mac_index(smc,1) ;
	mc->mc_receive_ct = smc->mib.m[MAC0].fddiMACCopied_Ct ;
	mc->mc_transmit_ct =  smc->mib.m[MAC0].fddiMACTransmit_Ct ;
}

/*
 * fill mac frame not copied counter
 */
static void smt_fill_mac_fnc(struct s_smc *smc, struct smt_p_mac_fnc *fnc)
{
	SMTSETPARA(fnc,SMT_P_MAC_FNC) ;
	fnc->nc_mib_index = INDEX_MAC ;
	fnc->nc_index = mac_index(smc,1) ;
	fnc->nc_counter = smc->mib.m[MAC0].fddiMACNotCopied_Ct ;
}


/*
 * fill manufacturer field
 */
static void smt_fill_manufacturer(struct s_smc *smc, 
				  struct smp_p_manufacturer *man)
{
	SMTSETPARA(man,SMT_P_MANUFACTURER) ;
	memcpy((char *) man->mf_data,
		(char *) smc->mib.fddiSMTManufacturerData,
		sizeof(man->mf_data)) ;
}

/*
 * fill user field
 */
static void smt_fill_user(struct s_smc *smc, struct smp_p_user *user)
{
	SMTSETPARA(user,SMT_P_USER) ;
	memcpy((char *) user->us_data,
		(char *) smc->mib.fddiSMTUserData,
		sizeof(user->us_data)) ;
}

/*
 * fill set count
 */
static void smt_fill_setcount(struct s_smc *smc, struct smt_p_setcount *setcount)
{
	SK_UNUSED(smc) ;
	SMTSETPARA(setcount,SMT_P_SETCOUNT) ;
	setcount->count = smc->mib.fddiSMTSetCount.count ;
	memcpy((char *)setcount->timestamp,
		(char *)smc->mib.fddiSMTSetCount.timestamp,8) ;
}

/*
 * fill echo data
 */
static void smt_fill_echo(struct s_smc *smc, struct smt_p_echo *echo, u_long seed,
			  int len)
{
	u_char	*p ;

	SK_UNUSED(smc) ;
	SMTSETPARA(echo,SMT_P_ECHODATA) ;
	echo->para.p_len = len ;
	for (p = echo->ec_data ; len ; len--) {
		*p++ = (u_char) seed ;
		seed += 13 ;
	}
}

/*
 * clear DNA and UNA
 * called from CFM if configuration changes
 */
static void smt_clear_una_dna(struct s_smc *smc)
{
	smc->mib.m[MAC0].fddiMACUpstreamNbr = SMT_Unknown ;
	smc->mib.m[MAC0].fddiMACDownstreamNbr = SMT_Unknown ;
}

static void smt_clear_old_una_dna(struct s_smc *smc)
{
	smc->mib.m[MAC0].fddiMACOldUpstreamNbr = SMT_Unknown ;
	smc->mib.m[MAC0].fddiMACOldDownstreamNbr = SMT_Unknown ;
}

u_long smt_get_tid(struct s_smc *smc)
{
	u_long	tid ;
	while ((tid = ++(smc->sm.smt_tid) ^ SMT_TID_MAGIC) == 0)
		;
	return tid & 0x3fffffffL;
}


/*
 * table of parameter lengths
 */
static const struct smt_pdef {
	int	ptype ;
	int	plen ;
	const char	*pswap ;
} smt_pdef[] = {
	{ SMT_P_UNA,	sizeof(struct smt_p_una) ,
		SWAP_SMT_P_UNA					} ,
	{ SMT_P_SDE,	sizeof(struct smt_p_sde) ,
		SWAP_SMT_P_SDE					} ,
	{ SMT_P_STATE,	sizeof(struct smt_p_state) ,
		SWAP_SMT_P_STATE				} ,
	{ SMT_P_TIMESTAMP,sizeof(struct smt_p_timestamp) ,
		SWAP_SMT_P_TIMESTAMP				} ,
	{ SMT_P_POLICY,	sizeof(struct smt_p_policy) ,
		SWAP_SMT_P_POLICY				} ,
	{ SMT_P_LATENCY,	sizeof(struct smt_p_latency) ,
		SWAP_SMT_P_LATENCY				} ,
	{ SMT_P_NEIGHBORS,sizeof(struct smt_p_neighbor) ,
		SWAP_SMT_P_NEIGHBORS				} ,
	{ SMT_P_PATH,	sizeof(struct smt_p_path) ,
		SWAP_SMT_P_PATH					} ,
	{ SMT_P_MAC_STATUS,sizeof(struct smt_p_mac_status) ,
		SWAP_SMT_P_MAC_STATUS				} ,
	{ SMT_P_LEM,	sizeof(struct smt_p_lem) ,
		SWAP_SMT_P_LEM					} ,
	{ SMT_P_MAC_COUNTER,sizeof(struct smt_p_mac_counter) ,
		SWAP_SMT_P_MAC_COUNTER				} ,
	{ SMT_P_MAC_FNC,sizeof(struct smt_p_mac_fnc) ,
		SWAP_SMT_P_MAC_FNC				} ,
	{ SMT_P_PRIORITY,sizeof(struct smt_p_priority) ,
		SWAP_SMT_P_PRIORITY				} ,
	{ SMT_P_EB,sizeof(struct smt_p_eb) ,
		SWAP_SMT_P_EB					} ,
	{ SMT_P_MANUFACTURER,sizeof(struct smp_p_manufacturer) ,
		SWAP_SMT_P_MANUFACTURER				} ,
	{ SMT_P_REASON,	sizeof(struct smt_p_reason) ,
		SWAP_SMT_P_REASON				} ,
	{ SMT_P_REFUSED, sizeof(struct smt_p_refused) ,
		SWAP_SMT_P_REFUSED				} ,
	{ SMT_P_VERSION, sizeof(struct smt_p_version) ,
		SWAP_SMT_P_VERSION				} ,
#ifdef ESS
	{ SMT_P0015, sizeof(struct smt_p_0015) , SWAP_SMT_P0015 } ,
	{ SMT_P0016, sizeof(struct smt_p_0016) , SWAP_SMT_P0016 } ,
	{ SMT_P0017, sizeof(struct smt_p_0017) , SWAP_SMT_P0017 } ,
	{ SMT_P0018, sizeof(struct smt_p_0018) , SWAP_SMT_P0018 } ,
	{ SMT_P0019, sizeof(struct smt_p_0019) , SWAP_SMT_P0019 } ,
	{ SMT_P001A, sizeof(struct smt_p_001a) , SWAP_SMT_P001A } ,
	{ SMT_P001B, sizeof(struct smt_p_001b) , SWAP_SMT_P001B } ,
	{ SMT_P001C, sizeof(struct smt_p_001c) , SWAP_SMT_P001C } ,
	{ SMT_P001D, sizeof(struct smt_p_001d) , SWAP_SMT_P001D } ,
#endif
#if	0
	{ SMT_P_FSC,	sizeof(struct smt_p_fsc) ,
		SWAP_SMT_P_FSC					} ,
#endif

	{ SMT_P_SETCOUNT,0,	SWAP_SMT_P_SETCOUNT		} ,
	{ SMT_P1048,	0,	SWAP_SMT_P1048			} ,
	{ SMT_P208C,	0,	SWAP_SMT_P208C			} ,
	{ SMT_P208D,	0,	SWAP_SMT_P208D			} ,
	{ SMT_P208E,	0,	SWAP_SMT_P208E			} ,
	{ SMT_P208F,	0,	SWAP_SMT_P208F			} ,
	{ SMT_P2090,	0,	SWAP_SMT_P2090			} ,
#ifdef	ESS
	{ SMT_P320B, sizeof(struct smt_p_320b) , SWAP_SMT_P320B } ,
	{ SMT_P320F, sizeof(struct smt_p_320f) , SWAP_SMT_P320F } ,
	{ SMT_P3210, sizeof(struct smt_p_3210) , SWAP_SMT_P3210 } ,
#endif
	{ SMT_P4050,	0,	SWAP_SMT_P4050			} ,
	{ SMT_P4051,	0,	SWAP_SMT_P4051			} ,
	{ SMT_P4052,	0,	SWAP_SMT_P4052			} ,
	{ SMT_P4053,	0,	SWAP_SMT_P4053			} ,
} ;

#define N_SMT_PLEN	ARRAY_SIZE(smt_pdef)

int smt_check_para(struct s_smc *smc, struct smt_header	*sm,
		   const u_short list[])
{
	const u_short		*p = list ;
	while (*p) {
		if (!sm_to_para(smc,sm,(int) *p)) {
			DB_SMT("SMT: smt_check_para - missing para %hx", *p);
			return -1;
		}
		p++ ;
	}
	return 0;
}

void *sm_to_para(struct s_smc *smc, struct smt_header *sm, int para)
{
	char	*p ;
	int	len ;
	int	plen ;
	void	*found = NULL;

	SK_UNUSED(smc) ;

	len = sm->smt_len ;
	p = (char *)(sm+1) ;		/* pointer to info */
	while (len > 0 ) {
		if (((struct smt_para *)p)->p_type == para)
			found = (void *) p ;
		plen = ((struct smt_para *)p)->p_len + PARA_LEN ;
		p += plen ;
		len -= plen ;
		if (len < 0) {
			DB_SMT("SMT : sm_to_para - length error %d", plen);
			return NULL;
		}
		if ((plen & 3) && (para != SMT_P_ECHODATA)) {
			DB_SMT("SMT : sm_to_para - odd length %d", plen);
			return NULL;
		}
		if (found)
			return found;
	}
	return NULL;
}

#if	0
/*
 * send ANTC data test frame
 */
void fddi_send_antc(struct s_smc *smc, struct fddi_addr *dest)
{
	SK_UNUSED(smc) ;
	SK_UNUSED(dest) ;
#if	0
	SMbuf			*mb ;
	struct smt_header	*smt ;
	int			i ;
	char			*p ;

	mb = smt_get_mbuf() ;
	mb->sm_len = 3000+12 ;
	p = smtod(mb, char *) + 12 ;
	for (i = 0 ; i < 3000 ; i++)
		*p++ = 1 << (i&7) ;

	smt = smtod(mb, struct smt_header *) ;
	smt->smt_dest = *dest ;
	smt->smt_source = smc->mib.m[MAC0].fddiMACSMTAddress ;
	smt_send_mbuf(smc,mb,FC_ASYNC_LLC) ;
#endif
}
#endif

#ifdef	DEBUG
char *addr_to_string(struct fddi_addr *addr)
{
	int	i ;
	static char	string[6*3] = "****" ;

	for (i = 0 ; i < 6 ; i++) {
		string[i * 3] = hex_asc_hi(addr->a[i]);
		string[i * 3 + 1] = hex_asc_lo(addr->a[i]);
		string[i * 3 + 2] = ':';
	}
	string[5 * 3 + 2] = 0;
	return string;
}
#endif

/*
 * return static mac index
 */
static int mac_index(struct s_smc *smc, int mac)
{
	SK_UNUSED(mac) ;
#ifdef	CONCENTRATOR
	SK_UNUSED(smc) ;
	return NUMPHYS + 1;
#else
	return (smc->s.sas == SMT_SAS) ? 2 : 3;
#endif
}

/*
 * return static phy index
 */
static int phy_index(struct s_smc *smc, int phy)
{
	SK_UNUSED(smc) ;
	return phy + 1;
}

/*
 * return dynamic mac connection resource index
 */
static int mac_con_resource_index(struct s_smc *smc, int mac)
{
#ifdef	CONCENTRATOR
	SK_UNUSED(smc) ;
	SK_UNUSED(mac) ;
	return entity_to_index(smc, cem_get_downstream(smc, ENTITY_MAC));
#else
	SK_UNUSED(mac) ;
	switch (smc->mib.fddiSMTCF_State) {
	case SC9_C_WRAP_A :
	case SC5_THRU_B :
	case SC11_C_WRAP_S :
		return 1;
	case SC10_C_WRAP_B :
	case SC4_THRU_A :
		return 2;
	}
	return smc->s.sas == SMT_SAS ? 2 : 3;
#endif
}

/*
 * return dynamic phy connection resource index
 */
static int phy_con_resource_index(struct s_smc *smc, int phy)
{
#ifdef	CONCENTRATOR
	return entity_to_index(smc, cem_get_downstream(smc, ENTITY_PHY(phy))) ;
#else
	switch (smc->mib.fddiSMTCF_State) {
	case SC9_C_WRAP_A :
		return phy == PA ? 3 : 2;
	case SC10_C_WRAP_B :
		return phy == PA ? 1 : 3;
	case SC4_THRU_A :
		return phy == PA ? 3 : 1;
	case SC5_THRU_B :
		return phy == PA ? 2 : 3;
	case SC11_C_WRAP_S :
		return 2;
	}
	return phy;
#endif
}

#ifdef	CONCENTRATOR
static int entity_to_index(struct s_smc *smc, int e)
{
	if (e == ENTITY_MAC)
		return mac_index(smc, 1);
	else
		return phy_index(smc, e - ENTITY_PHY(0));
}
#endif

#ifdef	LITTLE_ENDIAN
static int smt_swap_short(u_short s)
{
	return ((s>>8)&0xff) | ((s&0xff)<<8);
}

void smt_swap_para(struct smt_header *sm, int len, int direction)
/* int direction;	0 encode 1 decode */
{
	struct smt_para	*pa ;
	const  struct smt_pdef	*pd ;
	char	*p ;
	int	plen ;
	int	type ;
	int	i ;

/*	printf("smt_swap_para sm %x len %d dir %d\n",
		sm,len,direction) ;
 */
	smt_string_swap((char *)sm,SWAP_SMTHEADER,len) ;

	/* swap args */
	len -= sizeof(struct smt_header) ;

	p = (char *) (sm + 1) ;
	while (len > 0) {
		pa = (struct smt_para *) p ;
		plen = pa->p_len ;
		type = pa->p_type ;
		pa->p_type = smt_swap_short(pa->p_type) ;
		pa->p_len = smt_swap_short(pa->p_len) ;
		if (direction) {
			plen = pa->p_len ;
			type = pa->p_type ;
		}
		/*
		 * note: paras can have 0 length !
		 */
		if (plen < 0)
			break ;
		plen += PARA_LEN ;
		for (i = N_SMT_PLEN, pd = smt_pdef; i ; i--,pd++) {
			if (pd->ptype == type)
				break ;
		}
		if (i && pd->pswap) {
			smt_string_swap(p+PARA_LEN,pd->pswap,len) ;
		}
		len -= plen ;
		p += plen ;
	}
}

static void smt_string_swap(char *data, const char *format, int len)
{
	const char	*open_paren = NULL ;
	int	x ;

	while (len > 0  && *format) {
		switch (*format) {
		case '[' :
			open_paren = format ;
			break ;
		case ']' :
			format = open_paren ;
			break ;
		case '1' :
		case '2' :
		case '3' :
		case '4' :
		case '5' :
		case '6' :
		case '7' :
		case '8' :
		case '9' :
			data  += *format - '0' ;
			len   -= *format - '0' ;
			break ;
		case 'c':
			data++ ;
			len-- ;
			break ;
		case 's' :
			x = data[0] ;
			data[0] = data[1] ;
			data[1] = x ;
			data += 2 ;
			len -= 2 ;
			break ;
		case 'l' :
			x = data[0] ;
			data[0] = data[3] ;
			data[3] = x ;
			x = data[1] ;
			data[1] = data[2] ;
			data[2] = x ;
			data += 4 ;
			len -= 4 ;
			break ;
		}
		format++ ;
	}
}
#else
void smt_swap_para(struct smt_header *sm, int len, int direction)
/* int direction;	0 encode 1 decode */
{
	SK_UNUSED(sm) ;
	SK_UNUSED(len) ;
	SK_UNUSED(direction) ;
}
#endif

/*
 * PMF actions
 */
int smt_action(struct s_smc *smc, int class, int code, int index)
{
	int	event ;
	int	port ;
	DB_SMT("SMT: action %d code %d", class, code);
	switch(class) {
	case SMT_STATION_ACTION :
		switch(code) {
		case SMT_STATION_ACTION_CONNECT :
			smc->mib.fddiSMTRemoteDisconnectFlag = FALSE ;
			queue_event(smc,EVENT_ECM,EC_CONNECT) ;
			break ;
		case SMT_STATION_ACTION_DISCONNECT :
			queue_event(smc,EVENT_ECM,EC_DISCONNECT) ;
			smc->mib.fddiSMTRemoteDisconnectFlag = TRUE ;
			RS_SET(smc,RS_DISCONNECT) ;
			AIX_EVENT(smc, (u_long) FDDI_RING_STATUS, (u_long)
				FDDI_SMT_EVENT, (u_long) FDDI_REMOTE_DISCONNECT,
				smt_get_event_word(smc));
			break ;
		case SMT_STATION_ACTION_PATHTEST :
			AIX_EVENT(smc, (u_long) FDDI_RING_STATUS, (u_long)
				FDDI_SMT_EVENT, (u_long) FDDI_PATH_TEST,
				smt_get_event_word(smc));
			break ;
		case SMT_STATION_ACTION_SELFTEST :
			AIX_EVENT(smc, (u_long) FDDI_RING_STATUS, (u_long)
				FDDI_SMT_EVENT, (u_long) FDDI_REMOTE_SELF_TEST,
				smt_get_event_word(smc));
			break ;
		case SMT_STATION_ACTION_DISABLE_A :
			if (smc->y[PA].pc_mode == PM_PEER) {
				RS_SET(smc,RS_EVENT) ;
				queue_event(smc,EVENT_PCM+PA,PC_DISABLE) ;
			}
			break ;
		case SMT_STATION_ACTION_DISABLE_B :
			if (smc->y[PB].pc_mode == PM_PEER) {
				RS_SET(smc,RS_EVENT) ;
				queue_event(smc,EVENT_PCM+PB,PC_DISABLE) ;
			}
			break ;
		case SMT_STATION_ACTION_DISABLE_M :
			for (port = 0 ; port <  NUMPHYS ; port++) {
				if (smc->mib.p[port].fddiPORTMy_Type != TM)
					continue ;
				RS_SET(smc,RS_EVENT) ;
				queue_event(smc,EVENT_PCM+port,PC_DISABLE) ;
			}
			break ;
		default :
			return 1;
		}
		break ;
	case SMT_PORT_ACTION :
		switch(code) {
		case SMT_PORT_ACTION_ENABLE :
			event = PC_ENABLE ;
			break ;
		case SMT_PORT_ACTION_DISABLE :
			event = PC_DISABLE ;
			break ;
		case SMT_PORT_ACTION_MAINT :
			event = PC_MAINT ;
			break ;
		case SMT_PORT_ACTION_START :
			event = PC_START ;
			break ;
		case SMT_PORT_ACTION_STOP :
			event = PC_STOP ;
			break ;
		default :
			return 1;
		}
		queue_event(smc,EVENT_PCM+index,event) ;
		break ;
	default :
		return 1;
	}
	return 0;
}

/*
 * canonical conversion of <len> bytes beginning form *data
 */
#ifdef  USE_CAN_ADDR
static void hwm_conv_can(struct s_smc *smc, char *data, int len)
{
	int i ;

	SK_UNUSED(smc) ;

	for (i = len; i ; i--, data++)
		*data = bitrev8(*data);
}
#endif

#endif	/* no SLIM_SMT */

