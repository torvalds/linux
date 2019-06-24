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

/*
 * *******************************************************************
 * This SBA code implements the Synchronous Bandwidth Allocation
 * functions described in the "FDDI Synchronous Forum Implementer's
 * Agreement" dated December 1th, 1993.
 * *******************************************************************
 *
 *	PURPOSE: The purpose of this function is to control
 *		 synchronous allocations on a single FDDI segment.
 *		 Allocations are limited to the primary FDDI ring.
 *		 The SBM provides recovery mechanisms to recover
 *		 unused bandwidth also resolves T_Neg and
 *		 reconfiguration changes. Many of the SBM state
 *		 machine inputs are sourced by the underlying
 *		 FDDI sub-system supporting the SBA application.
 *
 * *******************************************************************
 */

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"
#include "h/smt_p.h"


#ifndef	SLIM_SMT

#ifdef ESS

#ifndef lint
static const char ID_sccs[] = "@(#)ess.c	1.10 96/02/23 (C) SK" ;
#define LINT_USE(x)
#else
#define LINT_USE(x)	(x)=(x)
#endif
#define MS2BCLK(x)	((x)*12500L)

/*
	-------------------------------------------------------------
	LOCAL VARIABLES:
	-------------------------------------------------------------
*/

static const u_short plist_raf_alc_res[] = { SMT_P0012, SMT_P320B, SMT_P320F,
					SMT_P3210, SMT_P0019, SMT_P001A,
					SMT_P001D, 0 } ;

static const u_short plist_raf_chg_req[] = { SMT_P320B, SMT_P320F, SMT_P3210,
					SMT_P001A, 0 } ;

static const struct fddi_addr smt_sba_da = {{0x80,0x01,0x43,0x00,0x80,0x0C}} ;
static const struct fddi_addr null_addr = {{0,0,0,0,0,0}} ;

/*
	-------------------------------------------------------------
	GLOBAL VARIABLES:
	-------------------------------------------------------------
*/


/*
	-------------------------------------------------------------
	LOCAL FUNCTIONS:
	-------------------------------------------------------------
*/

static void ess_send_response(struct s_smc *smc, struct smt_header *sm,
			      int sba_cmd);
static void ess_config_fifo(struct s_smc *smc);
static void ess_send_alc_req(struct s_smc *smc);
static void ess_send_frame(struct s_smc *smc, SMbuf *mb);

/*
	-------------------------------------------------------------
	EXTERNAL FUNCTIONS:
	-------------------------------------------------------------
*/

/*
	-------------------------------------------------------------
	PUBLIC FUNCTIONS:
	-------------------------------------------------------------
*/

void ess_timer_poll(struct s_smc *smc);
void ess_para_change(struct s_smc *smc);
int ess_raf_received_pack(struct s_smc *smc, SMbuf *mb, struct smt_header *sm,
			  int fs);
static int process_bw_alloc(struct s_smc *smc, long int payload, long int overhead);


/*
 * --------------------------------------------------------------------------
 *	End Station Support	(ESS)
 * --------------------------------------------------------------------------
 */

/*
 * evaluate the RAF frame
 */
int ess_raf_received_pack(struct s_smc *smc, SMbuf *mb, struct smt_header *sm,
			  int fs)
{
	void			*p ;		/* universal pointer */
	struct smt_p_0016	*cmd ;		/* para: command for the ESS */
	SMbuf			*db ;
	u_long			msg_res_type ;	/* recource type */
	u_long			payload, overhead ;
	int			local ;
	int			i ;

	/*
	 * Message Processing Code
	 */
	 local = ((fs & L_INDICATOR) != 0) ;

	/*
	 * get the resource type
	 */
	if (!(p = (void *) sm_to_para(smc,sm,SMT_P0015))) {
		DB_ESS("ESS: RAF frame error, parameter type not found");
		return fs;
	}
	msg_res_type = ((struct smt_p_0015 *)p)->res_type ;

	/*
	 * get the pointer to the ESS command
	 */
	if (!(cmd = (struct smt_p_0016 *) sm_to_para(smc,sm,SMT_P0016))) {
		/*
		 * error in frame: para ESS command was not found
		 */
		 DB_ESS("ESS: RAF frame error, parameter command not found");
		 return fs;
	}

	DB_ESSN(2, "fc %x	ft %x", sm->smt_class, sm->smt_type);
	DB_ESSN(2, "ver %x	tran %x", sm->smt_version, sm->smt_tid);
	DB_ESSN(2, "stn_id %s", addr_to_string(&sm->smt_source));

	DB_ESSN(2, "infolen %x	res %lx", sm->smt_len, msg_res_type);
	DB_ESSN(2, "sbacmd %x", cmd->sba_cmd);

	/*
	 * evaluate the ESS command
	 */
	switch (cmd->sba_cmd) {

	/*
	 * Process an ESS Allocation Request
	 */
	case REQUEST_ALLOCATION :
		/*
		 * check for an RAF Request (Allocation Request)
		 */
		if (sm->smt_type == SMT_REQUEST) {
			/*
			 * process the Allocation request only if the frame is
			 * local and no static allocation is used
			 */
			if (!local || smc->mib.fddiESSPayload)
				return fs;
			
			p = (void *) sm_to_para(smc,sm,SMT_P0019)  ;
			for (i = 0; i < 5; i++) {
				if (((struct smt_p_0019 *)p)->alloc_addr.a[i]) {
					return fs;
				}
			}

			/*
			 * Note: The Application should send a LAN_LOC_FRAME.
			 *	 The ESS do not send the Frame to the network!
			 */
			smc->ess.alloc_trans_id = sm->smt_tid ;
			DB_ESS("ESS: save Alloc Req Trans ID %x", sm->smt_tid);
			p = (void *) sm_to_para(smc,sm,SMT_P320F) ;
			((struct smt_p_320f *)p)->mib_payload =
				smc->mib.a[PATH0].fddiPATHSbaPayload ;
			p = (void *) sm_to_para(smc,sm,SMT_P3210) ;
			((struct smt_p_3210 *)p)->mib_overhead =
				smc->mib.a[PATH0].fddiPATHSbaOverhead ;
			sm->smt_dest = smt_sba_da ;

			if (smc->ess.local_sba_active)
				return fs | I_INDICATOR;

			if (!(db = smt_get_mbuf(smc)))
				return fs;

			db->sm_len = mb->sm_len ;
			db->sm_off = mb->sm_off ;
			memcpy(((char *)(db->sm_data+db->sm_off)),(char *)sm,
				(int)db->sm_len) ;
			dump_smt(smc,
				(struct smt_header *)(db->sm_data+db->sm_off),
				"RAF") ;
			smt_send_frame(smc,db,FC_SMT_INFO,0) ;
			return fs;
		}

		/*
		 * The RAF frame is an Allocation Response !
		 * check the parameters
		 */
		if (smt_check_para(smc,sm,plist_raf_alc_res)) {
			DB_ESS("ESS: RAF with para problem, ignoring");
			return fs;
		}

		/*
		 * VERIFY THE FRAME IS WELL BUILT:
		 *
		 *	1. path index = primary ring only
		 *	2. resource type = sync bw only
		 *	3. trans action id = alloc_trans_id
		 *	4. reason code = success
		 *
		 * If any are violated, discard the RAF frame
		 */
		if ((((struct smt_p_320b *)sm_to_para(smc,sm,SMT_P320B))->path_index
			!= PRIMARY_RING) ||
			(msg_res_type != SYNC_BW) ||
		(((struct smt_p_reason *)sm_to_para(smc,sm,SMT_P0012))->rdf_reason
			!= SMT_RDF_SUCCESS) ||
			(sm->smt_tid != smc->ess.alloc_trans_id)) {

			DB_ESS("ESS: Allocation Response not accepted");
			return fs;
		}

		/*
		 * Extract message parameters
		 */
		p = (void *) sm_to_para(smc,sm,SMT_P320F) ;
                if (!p) {
                        printk(KERN_ERR "ESS: sm_to_para failed");
                        return fs;
                }       
		payload = ((struct smt_p_320f *)p)->mib_payload ;
		p = (void *) sm_to_para(smc,sm,SMT_P3210) ;
                if (!p) {
                        printk(KERN_ERR "ESS: sm_to_para failed");
                        return fs;
                }       
		overhead = ((struct smt_p_3210 *)p)->mib_overhead ;

		DB_ESSN(2, "payload= %lx	overhead= %lx",
			payload, overhead);

		/*
		 * process the bandwidth allocation
		 */
		(void)process_bw_alloc(smc,(long)payload,(long)overhead) ;

		return fs;
		/* end of Process Allocation Request */

	/*
	 * Process an ESS Change Request
	 */
	case CHANGE_ALLOCATION :
		/*
		 * except only replies
		 */
		if (sm->smt_type != SMT_REQUEST) {
			DB_ESS("ESS: Do not process Change Responses");
			return fs;
		}

		/*
		 * check the para for the Change Request
		 */
		if (smt_check_para(smc,sm,plist_raf_chg_req)) {
			DB_ESS("ESS: RAF with para problem, ignoring");
			return fs;
		}

		/*
		 * Verify the path index and resource
		 * type are correct. If any of
		 * these are false, don't process this
		 * change request frame.
		 */
		if ((((struct smt_p_320b *)sm_to_para(smc,sm,SMT_P320B))->path_index
			!= PRIMARY_RING) || (msg_res_type != SYNC_BW)) {
			DB_ESS("ESS: RAF frame with para problem, ignoring");
			return fs;
		}

		/*
		 * Extract message queue parameters
		 */
		p = (void *) sm_to_para(smc,sm,SMT_P320F) ;
		payload = ((struct smt_p_320f *)p)->mib_payload ;
		p = (void *) sm_to_para(smc,sm,SMT_P3210) ;
		overhead = ((struct smt_p_3210 *)p)->mib_overhead ;

		DB_ESSN(2, "ESS: Change Request from %s",
			addr_to_string(&sm->smt_source));
		DB_ESSN(2, "payload= %lx	overhead= %lx",
			payload, overhead);

		/*
		 * process the bandwidth allocation
		 */
		if(!process_bw_alloc(smc,(long)payload,(long)overhead))
			return fs;

		/*
		 * send an RAF Change Reply
		 */
		ess_send_response(smc,sm,CHANGE_ALLOCATION) ;

		return fs;
		/* end of Process Change Request */

	/*
	 * Process Report Response
	 */
	case REPORT_ALLOCATION :
		/*
		 * except only requests
		 */
		if (sm->smt_type != SMT_REQUEST) {
			DB_ESS("ESS: Do not process a Report Reply");
			return fs;
		}

		DB_ESSN(2, "ESS: Report Request from %s",
			addr_to_string(&sm->smt_source));

		/*
		 * verify that the resource type is sync bw only
		 */
		if (msg_res_type != SYNC_BW) {
			DB_ESS("ESS: ignoring RAF with para problem");
			return fs;
		}

		/*
		 * send an RAF Change Reply
		 */
		ess_send_response(smc,sm,REPORT_ALLOCATION) ;

		return fs;
		/* end of Process Report Request */

	default:
		/*
		 * error in frame
		 */
		DB_ESS("ESS: ignoring RAF with bad sba_cmd");
		break ;
	}

	return fs;
}

/*
 * determines the synchronous bandwidth, set the TSYNC register and the
 * mib variables SBAPayload, SBAOverhead and fddiMACT-NEG.
 */
static int process_bw_alloc(struct s_smc *smc, long int payload, long int overhead)
{
	/*
	 * determine the synchronous bandwidth (sync_bw) in bytes per T-NEG,
	 * if the payload is greater than zero.
	 * For the SBAPayload and the SBAOverhead we have the following
	 * unite quations
 	 *		      _		  _
	 *		     |	     bytes |
	 *	SBAPayload = | 8000 ------ |
	 *		     |		s  |
	 *		      -		  -
 	 *		       _       _
	 *		      |	 bytes	|
	 *	SBAOverhead = | ------	|
	 *		      |	 T-NEG	|
	 *		       -       -
 	 *
	 * T-NEG is described by the equation:
	 *
	 *		     (-) fddiMACT-NEG
	 *	T-NEG =	    -------------------
	 *			12500000 1/s
	 *
	 * The number of bytes we are able to send is the payload
	 * plus the overhead.
	 *
	 *			  bytes    T-NEG SBAPayload 8000 bytes/s
	 * sync_bw =  SBAOverhead ------ + -----------------------------
	 *	   		  T-NEG		T-NEG
	 *
	 *
	 *	      		     1
	 * sync_bw =  SBAOverhead + ---- (-)fddiMACT-NEG * SBAPayload
	 *	       		    1562
	 *
	 */

	/*
	 * set the mib attributes fddiPATHSbaOverhead, fddiPATHSbaPayload
	 */
/*	if (smt_set_obj(smc,SMT_P320F,payload,S_SET)) {
		DB_ESS("ESS: SMT does not accept the payload value");
		return FALSE;
	}
	if (smt_set_obj(smc,SMT_P3210,overhead,S_SET)) {
		DB_ESS("ESS: SMT does not accept the overhead value");
		return FALSE;
	} */

	/* premliminary */
	if (payload > MAX_PAYLOAD || overhead > 5000) {
		DB_ESS("ESS: payload / overhead not accepted");
		return FALSE;
	}

	/*
	 * start the iterative allocation process if the payload or the overhead
	 * are smaller than the parsed values
	 */
	if (smc->mib.fddiESSPayload &&
		((u_long)payload != smc->mib.fddiESSPayload ||
		(u_long)overhead != smc->mib.fddiESSOverhead)) {
		smc->ess.raf_act_timer_poll = TRUE ;
		smc->ess.timer_count = 0 ;
	}

	/*
	 * evulate the Payload
	 */
	if (payload) {
		DB_ESSN(2, "ESS: turn SMT_ST_SYNC_SERVICE bit on");
		smc->ess.sync_bw_available = TRUE ;

		smc->ess.sync_bw = overhead -
			(long)smc->mib.m[MAC0].fddiMACT_Neg *
			payload / 1562 ;
	}
	else {
		DB_ESSN(2, "ESS: turn SMT_ST_SYNC_SERVICE bit off");
		smc->ess.sync_bw_available = FALSE ;
		smc->ess.sync_bw = 0 ;
		overhead = 0 ;
	}

	smc->mib.a[PATH0].fddiPATHSbaPayload = payload ;
	smc->mib.a[PATH0].fddiPATHSbaOverhead = overhead ;


	DB_ESSN(2, "tsync = %lx", smc->ess.sync_bw);

	ess_config_fifo(smc) ;
	set_formac_tsync(smc,smc->ess.sync_bw) ;
	return TRUE;
}

static void ess_send_response(struct s_smc *smc, struct smt_header *sm,
			      int sba_cmd)
{
	struct smt_sba_chg	*chg ;
	SMbuf			*mb ;
	void			*p ;

	/*
	 * get and initialize the response frame
	 */
	if (sba_cmd == CHANGE_ALLOCATION) {
		if (!(mb=smt_build_frame(smc,SMT_RAF,SMT_REPLY,
				sizeof(struct smt_sba_chg))))
				return ;
	}
	else {
		if (!(mb=smt_build_frame(smc,SMT_RAF,SMT_REPLY,
				sizeof(struct smt_sba_rep_res))))
				return ;
	}

	chg = smtod(mb,struct smt_sba_chg *) ;
	chg->smt.smt_tid = sm->smt_tid ;
	chg->smt.smt_dest = sm->smt_source ;

	/* set P15 */
	chg->s_type.para.p_type = SMT_P0015 ;
	chg->s_type.para.p_len = sizeof(struct smt_p_0015) - PARA_LEN ;
	chg->s_type.res_type = SYNC_BW ;

	/* set P16 */
	chg->cmd.para.p_type = SMT_P0016 ;
	chg->cmd.para.p_len = sizeof(struct smt_p_0016) - PARA_LEN ;
	chg->cmd.sba_cmd = sba_cmd ;

	/* set P320B */
	chg->path.para.p_type = SMT_P320B ;
	chg->path.para.p_len = sizeof(struct smt_p_320b) - PARA_LEN ;
	chg->path.mib_index = SBAPATHINDEX ;
	chg->path.path_pad = 0;
	chg->path.path_index = PRIMARY_RING ;

	/* set P320F */
	chg->payload.para.p_type = SMT_P320F ;
	chg->payload.para.p_len = sizeof(struct smt_p_320f) - PARA_LEN ;
	chg->payload.mib_index = SBAPATHINDEX ;
	chg->payload.mib_payload = smc->mib.a[PATH0].fddiPATHSbaPayload ;

	/* set P3210 */
	chg->overhead.para.p_type = SMT_P3210 ;
	chg->overhead.para.p_len = sizeof(struct smt_p_3210) - PARA_LEN ;
	chg->overhead.mib_index = SBAPATHINDEX ;
	chg->overhead.mib_overhead = smc->mib.a[PATH0].fddiPATHSbaOverhead ;

	if (sba_cmd == CHANGE_ALLOCATION) {
		/* set P1A */
		chg->cat.para.p_type = SMT_P001A ;
		chg->cat.para.p_len = sizeof(struct smt_p_001a) - PARA_LEN ;
		p = (void *) sm_to_para(smc,sm,SMT_P001A) ;
		chg->cat.category = ((struct smt_p_001a *)p)->category ;
	}
	dump_smt(smc,(struct smt_header *)chg,"RAF") ;
	ess_send_frame(smc,mb) ;
}

void ess_timer_poll(struct s_smc *smc)
{
	if (!smc->ess.raf_act_timer_poll)
		return ;

	DB_ESSN(2, "ESS: timer_poll");

	smc->ess.timer_count++ ;
	if (smc->ess.timer_count == 10) {
		smc->ess.timer_count = 0 ;
		ess_send_alc_req(smc) ;
	}
}

static void ess_send_alc_req(struct s_smc *smc)
{
	struct smt_sba_alc_req *req ;
	SMbuf	*mb ;

	/*
	 * send never allocation request where the requested payload and
	 * overhead is zero or deallocate bandwidth when no bandwidth is
	 * parsed
	 */
	if (!smc->mib.fddiESSPayload) {
		smc->mib.fddiESSOverhead = 0 ;
	}
	else {
		if (!smc->mib.fddiESSOverhead)
			smc->mib.fddiESSOverhead = DEFAULT_OV ;
	}

	if (smc->mib.fddiESSOverhead ==
		smc->mib.a[PATH0].fddiPATHSbaOverhead &&
		smc->mib.fddiESSPayload ==
		smc->mib.a[PATH0].fddiPATHSbaPayload){
		smc->ess.raf_act_timer_poll = FALSE ;
		smc->ess.timer_count = 7 ;	/* next RAF alc req after 3 s */
		return ;
	}
	
	/*
	 * get and initialize the response frame
	 */
	if (!(mb=smt_build_frame(smc,SMT_RAF,SMT_REQUEST,
			sizeof(struct smt_sba_alc_req))))
			return ;
	req = smtod(mb,struct smt_sba_alc_req *) ;
	req->smt.smt_tid = smc->ess.alloc_trans_id = smt_get_tid(smc) ;
	req->smt.smt_dest = smt_sba_da ;

	/* set P15 */
	req->s_type.para.p_type = SMT_P0015 ;
	req->s_type.para.p_len = sizeof(struct smt_p_0015) - PARA_LEN ;
	req->s_type.res_type = SYNC_BW ;

	/* set P16 */
	req->cmd.para.p_type = SMT_P0016 ;
	req->cmd.para.p_len = sizeof(struct smt_p_0016) - PARA_LEN ;
	req->cmd.sba_cmd = REQUEST_ALLOCATION ;

	/*
	 * set the parameter type and parameter length of all used
	 * parameters
	 */

	/* set P320B */
	req->path.para.p_type = SMT_P320B ;
	req->path.para.p_len = sizeof(struct smt_p_320b) - PARA_LEN ;
	req->path.mib_index = SBAPATHINDEX ;
	req->path.path_pad = 0;
	req->path.path_index = PRIMARY_RING ;

	/* set P0017 */
	req->pl_req.para.p_type = SMT_P0017 ;
	req->pl_req.para.p_len = sizeof(struct smt_p_0017) - PARA_LEN ;
	req->pl_req.sba_pl_req = smc->mib.fddiESSPayload -
		smc->mib.a[PATH0].fddiPATHSbaPayload ;

	/* set P0018 */
	req->ov_req.para.p_type = SMT_P0018 ;
	req->ov_req.para.p_len = sizeof(struct smt_p_0018) - PARA_LEN ;
	req->ov_req.sba_ov_req = smc->mib.fddiESSOverhead -
		smc->mib.a[PATH0].fddiPATHSbaOverhead ;

	/* set P320F */
	req->payload.para.p_type = SMT_P320F ;
	req->payload.para.p_len = sizeof(struct smt_p_320f) - PARA_LEN ;
	req->payload.mib_index = SBAPATHINDEX ;
	req->payload.mib_payload = smc->mib.a[PATH0].fddiPATHSbaPayload ;

	/* set P3210 */
	req->overhead.para.p_type = SMT_P3210 ;
	req->overhead.para.p_len = sizeof(struct smt_p_3210) - PARA_LEN ;
	req->overhead.mib_index = SBAPATHINDEX ;
	req->overhead.mib_overhead = smc->mib.a[PATH0].fddiPATHSbaOverhead ;

	/* set P19 */
	req->a_addr.para.p_type = SMT_P0019 ;
	req->a_addr.para.p_len = sizeof(struct smt_p_0019) - PARA_LEN ;
	req->a_addr.sba_pad = 0;
	req->a_addr.alloc_addr = null_addr ;

	/* set P1A */
	req->cat.para.p_type = SMT_P001A ;
	req->cat.para.p_len = sizeof(struct smt_p_001a) - PARA_LEN ;
	req->cat.category = smc->mib.fddiESSCategory ;

	/* set P1B */
	req->tneg.para.p_type = SMT_P001B ;
	req->tneg.para.p_len = sizeof(struct smt_p_001b) - PARA_LEN ;
	req->tneg.max_t_neg = smc->mib.fddiESSMaxTNeg ;

	/* set P1C */
	req->segm.para.p_type = SMT_P001C ;
	req->segm.para.p_len = sizeof(struct smt_p_001c) - PARA_LEN ;
	req->segm.min_seg_siz = smc->mib.fddiESSMinSegmentSize ;

	dump_smt(smc,(struct smt_header *)req,"RAF") ;
	ess_send_frame(smc,mb) ;
}

static void ess_send_frame(struct s_smc *smc, SMbuf *mb)
{
	/*
	 * check if the frame must be send to the own ESS
	 */
	if (smc->ess.local_sba_active) {
		/*
		 * Send the Change Reply to the local SBA
		 */
		DB_ESS("ESS:Send to the local SBA");
		if (!smc->ess.sba_reply_pend)
			smc->ess.sba_reply_pend = mb ;
		else {
			DB_ESS("Frame is lost - another frame was pending");
			smt_free_mbuf(smc,mb) ;
		}
	}
	else {
		/*
		 * Send the SBA RAF Change Reply to the network
		 */
		DB_ESS("ESS:Send to the network");
		smt_send_frame(smc,mb,FC_SMT_INFO,0) ;
	}
}

void ess_para_change(struct s_smc *smc)
{
	(void)process_bw_alloc(smc,(long)smc->mib.a[PATH0].fddiPATHSbaPayload,
		(long)smc->mib.a[PATH0].fddiPATHSbaOverhead) ;
}

static void ess_config_fifo(struct s_smc *smc)
{
	/*
	 * if nothing to do exit 
	 */
	if (smc->mib.a[PATH0].fddiPATHSbaPayload) {
		if (smc->hw.fp.fifo.fifo_config_mode & SYNC_TRAFFIC_ON &&
			(smc->hw.fp.fifo.fifo_config_mode&SEND_ASYNC_AS_SYNC) ==
			smc->mib.fddiESSSynchTxMode) {
			return ;
		}
	}
	else {
		if (!(smc->hw.fp.fifo.fifo_config_mode & SYNC_TRAFFIC_ON)) {
			return ;
		}
	}

	/*
	 * split up the FIFO and reinitialize the queues
	 */
	formac_reinit_tx(smc) ;
}

#endif /* ESS */

#endif	/* no SLIM_SMT */

