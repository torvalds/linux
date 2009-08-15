/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
 *	SMT 7.2 frame definitions
 */

#ifndef	_SMT_
#define _SMT_

/* #define SMT5_10 */
#define SMT6_10
#define SMT7_20

#define	OPT_PMF		/* if parameter management is supported */
#define	OPT_SRF		/* if status report is supported */

/*
 * SMT frame version 5.1
 */

#define SMT_VID	0x0001			/* V 5.1 .. 6.1 */
#define SMT_VID_2 0x0002		/* V 7.2 */

struct smt_sid {
	u_char	sid_oem[2] ;			/* implementation spec. */
	struct fddi_addr sid_node ;		/* node address */
} ;

typedef u_char	t_station_id[8] ;

/*
 * note on alignment :
 * sizeof(struct smt_header) = 32
 * all parameters are long aligned
 * if struct smt_header starts at offset 0, all longs are aligned correctly
 * (FC starts at offset 3)
 */
_packed struct smt_header {
	struct fddi_addr    	smt_dest ;	/* destination address */
	struct fddi_addr	smt_source ;	/* source address */
	u_char			smt_class ;	/* NIF, SIF ... */
	u_char			smt_type ;	/* req., response .. */
	u_short			smt_version ;	/* version id */
	u_int			smt_tid ;	/* transaction ID */
	struct smt_sid		smt_sid ;	/* station ID */
	u_short			smt_pad ;	/* pad with 0 */
	u_short			smt_len ;	/* length of info field */
} ;
#define SWAP_SMTHEADER	"662sl8ss"

#if	0
/*
 * MAC FC values
 */
#define FC_SMT_INFO	0x41		/* SMT info */
#define FC_SMT_NSA	0x4f		/* SMT Next Station Addressing */
#endif


/*
 * type codes
 */
#define SMT_ANNOUNCE	0x01		/* announcement */
#define SMT_REQUEST	0x02		/* request */
#define SMT_REPLY	0x03		/* reply */

/*
 * class codes
 */
#define SMT_NIF		0x01		/* neighbor information frames */
#define SMT_SIF_CONFIG	0x02		/* station information configuration */
#define SMT_SIF_OPER	0x03		/* station information operation */
#define SMT_ECF		0x04		/* echo frames */
#define SMT_RAF		0x05		/* resource allocation */
#define SMT_RDF		0x06		/* request denied */
#define SMT_SRF		0x07		/* status report */
#define SMT_PMF_GET	0x08		/* parameter management get */
#define SMT_PMF_SET	0x09		/* parameter management set */
#define SMT_ESF		0xff		/* extended service */

#define SMT_MAX_ECHO_LEN	4458	/* max length of SMT Echo */
#if	defined(CONC) || defined(CONC_II)
#define SMT_TEST_ECHO_LEN	50	/* test length of SMT Echo */
#else
#define SMT_TEST_ECHO_LEN	SMT_MAX_ECHO_LEN	/* test length */
#endif

#define SMT_MAX_INFO_LEN	(4352-20)	/* max length for SMT info */


/*
 * parameter types
 */

struct smt_para {
	u_short	p_type ;		/* type */
	u_short	p_len ;			/* length of parameter */
} ;

#define PARA_LEN	(sizeof(struct smt_para))

#define SMTSETPARA(p,t)		(p)->para.p_type = (t),\
				(p)->para.p_len = sizeof(*(p)) - PARA_LEN

/*
 * P01 : Upstream Neighbor Address, UNA
 */
#define SMT_P_UNA	0x0001		/* upstream neighbor address */
#define SWAP_SMT_P_UNA	"s6"

struct smt_p_una {
	struct smt_para	para ;		/* generic parameter header */
	u_short	una_pad ;
	struct fddi_addr una_node ;	/* node address, zero if unknown */
} ;

/*
 * P02 : Station Descriptor
 */
#define SMT_P_SDE	0x0002		/* station descriptor */
#define SWAP_SMT_P_SDE	"1111"

#define SMT_SDE_STATION		0	/* end node */
#define SMT_SDE_CONCENTRATOR	1	/* concentrator */

struct smt_p_sde {
	struct smt_para	para ;		/* generic parameter header */
	u_char	sde_type ;		/* station type */
	u_char	sde_mac_count ;		/* number of MACs */
	u_char	sde_non_master ;	/* number of A,B or S ports */
	u_char	sde_master ;		/* number of S ports on conc. */
} ;

/*
 * P03 : Station State
 */
#define SMT_P_STATE	0x0003		/* station state */
#define SWAP_SMT_P_STATE	"scc"

struct smt_p_state {
	struct smt_para	para ;		/* generic parameter header */
	u_short	st_pad ;
	u_char	st_topology ;		/* topology */
	u_char	st_dupl_addr ;		/* duplicate address detected */
} ;
#define SMT_ST_WRAPPED		(1<<0)	/* station wrapped */
#define SMT_ST_UNATTACHED	(1<<1)	/* unattached concentrator */
#define SMT_ST_TWISTED_A	(1<<2)	/* A-A connection, twisted ring */
#define SMT_ST_TWISTED_B	(1<<3)	/* B-B connection, twisted ring */
#define SMT_ST_ROOTED_S		(1<<4)	/* rooted station */
#define SMT_ST_SRF		(1<<5)	/* SRF protocol supported */
#define SMT_ST_SYNC_SERVICE	(1<<6)	/* use synchronous bandwidth */

#define SMT_ST_MY_DUPA		(1<<0)	/* my station detected dupl. */
#define SMT_ST_UNA_DUPA		(1<<1)	/* my UNA detected duplicate */

/*
 * P04 : timestamp
 */
#define SMT_P_TIMESTAMP	0x0004		/* time stamp */
#define SWAP_SMT_P_TIMESTAMP	"8"
struct smt_p_timestamp {
	struct smt_para	para ;		/* generic parameter header */
	u_char	ts_time[8] ;		/* time, resolution 80nS, unique */
} ;

/*
 * P05 : station policies
 */
#define SMT_P_POLICY	0x0005		/* station policies */
#define SWAP_SMT_P_POLICY	"ss"

struct smt_p_policy {
	struct smt_para	para ;		/* generic parameter header */
	u_short	pl_config ;
	u_short pl_connect ;		/* bit string POLICY_AA ... */
} ;
#define SMT_PL_HOLD		1	/* hold policy supported (Dual MAC) */

/*
 * P06 : latency equivalent
 */
#define SMT_P_LATENCY	0x0006		/* latency */
#define SWAP_SMT_P_LATENCY	"ssss"

/*
 * note: latency has two phy entries by definition
 * for a SAS, the 2nd one is null
 */
struct smt_p_latency {
	struct smt_para	para ;		/* generic parameter header */
	u_short	lt_phyout_idx1 ;	/* index */
	u_short	lt_latency1 ;		/* latency , unit : byte clock */
	u_short	lt_phyout_idx2 ;	/* 0 if SAS */
	u_short	lt_latency2 ;		/* 0 if SAS */
} ;

/*
 * P07 : MAC neighbors
 */
#define SMT_P_NEIGHBORS	0x0007		/* MAC neighbor description */
#define SWAP_SMT_P_NEIGHBORS	"ss66"

struct smt_p_neighbor {
	struct smt_para	para ;		/* generic parameter header */
	u_short	nb_mib_index ;		/* MIB index */
	u_short	nb_mac_index ;		/* n+1 .. n+m, m = #MACs, n = #PHYs */
	struct fddi_addr nb_una ;	/* UNA , 0 for unknown */
	struct fddi_addr nb_dna ;	/* DNA , 0 for unknown */
} ;

/*
 * PHY record
 */
#define SMT_PHY_A	0		/* A port */
#define SMT_PHY_B	1		/* B port */
#define SMT_PHY_S	2		/* slave port */
#define SMT_PHY_M	3		/* master port */

#define SMT_CS_DISABLED	0		/* connect state : disabled */
#define SMT_CS_CONNECTING	1	/* connect state : connecting */
#define SMT_CS_STANDBY	2		/* connect state : stand by */
#define SMT_CS_ACTIVE	3		/* connect state : active */

#define SMT_RM_NONE	0
#define SMT_RM_MAC	1

struct smt_phy_rec {
	u_short	phy_mib_index ;		/* MIB index */
	u_char	phy_type ;		/* A/B/S/M */
	u_char	phy_connect_state ;	/* disabled/connecting/active */
	u_char	phy_remote_type ;	/* A/B/S/M */
	u_char	phy_remote_mac ;	/* none/remote */
	u_short	phy_resource_idx ;	/* 1 .. n */
} ;

/*
 * MAC record
 */
struct smt_mac_rec {
	struct fddi_addr mac_addr ;		/* MAC address */
	u_short		mac_resource_idx ;	/* n+1 .. n+m */
} ;

/*
 * P08 : path descriptors
 * should be really an array ; however our environment has a fixed number of
 * PHYs and MACs
 */
#define SMT_P_PATH	0x0008			/* path descriptor */
#define SWAP_SMT_P_PATH	"[6s]"

struct smt_p_path {
	struct smt_para	para ;		/* generic parameter header */
	struct smt_phy_rec	pd_phy[2] ;	/* PHY A */
	struct smt_mac_rec	pd_mac ;	/* MAC record */
} ;

/*
 * P09 : MAC status
 */
#define SMT_P_MAC_STATUS	0x0009		/* MAC status */
#define SWAP_SMT_P_MAC_STATUS	"sslllllllll"

struct smt_p_mac_status {
	struct smt_para	para ;		/* generic parameter header */
	u_short st_mib_index ;		/* MIB index */
	u_short	st_mac_index ;		/* n+1 .. n+m */
	u_int	st_t_req ;		/* T_Req */
	u_int	st_t_neg ;		/* T_Neg */
	u_int	st_t_max ;		/* T_Max */
	u_int	st_tvx_value ;		/* TVX_Value */
	u_int	st_t_min ;		/* T_Min */
	u_int	st_sba ;		/* synchr. bandwidth alloc */
	u_int	st_frame_ct ;		/* frame counter */
	u_int	st_error_ct ;		/* error counter */
	u_int	st_lost_ct ;		/* lost frames counter */
} ;

/*
 * P0A : PHY link error rate monitoring
 */
#define SMT_P_LEM	0x000a		/* link error monitor */
#define SWAP_SMT_P_LEM	"ssccccll"
/*
 * units of lem_cutoff,lem_alarm,lem_estimate : 10**-x
 */
struct smt_p_lem {
	struct smt_para	para ;		/* generic parameter header */
	u_short	lem_mib_index ;		/* MIB index */
	u_short	lem_phy_index ;		/* 1 .. n */
	u_char	lem_pad2 ;		/* be nice and make it even . */
	u_char	lem_cutoff ;		/* 0x4 .. 0xf, default 0x7 */
	u_char	lem_alarm ;		/* 0x4 .. 0xf, default 0x8 */
	u_char	lem_estimate ;		/* 0x0 .. 0xff */
	u_int	lem_reject_ct ;		/* 0x00000000 .. 0xffffffff */
	u_int	lem_ct ;		/* 0x00000000 .. 0xffffffff */
} ;

/*
 * P0B : MAC frame counters
 */
#define SMT_P_MAC_COUNTER 0x000b	/* MAC frame counters */
#define SWAP_SMT_P_MAC_COUNTER	"ssll"

struct smt_p_mac_counter {
	struct smt_para	para ;		/* generic parameter header */
	u_short	mc_mib_index ;		/* MIB index */
	u_short	mc_index ;		/* mac index */
	u_int	mc_receive_ct ;		/* receive counter */
	u_int	mc_transmit_ct ;	/* transmit counter */
} ;

/*
 * P0C : MAC frame not copied counter
 */
#define SMT_P_MAC_FNC	0x000c		/* MAC frame not copied counter */
#define SWAP_SMT_P_MAC_FNC	"ssl"

struct smt_p_mac_fnc {
	struct smt_para	para ;		/* generic parameter header */
	u_short	nc_mib_index ;		/* MIB index */
	u_short	nc_index ;		/* mac index */
	u_int	nc_counter ;		/* not copied counter */
} ;


/*
 * P0D : MAC priority values
 */
#define SMT_P_PRIORITY	0x000d		/* MAC priority values */
#define SWAP_SMT_P_PRIORITY	"ssl"

struct smt_p_priority {
	struct smt_para	para ;		/* generic parameter header */
	u_short	pr_mib_index ;		/* MIB index */
	u_short	pr_index ;		/* mac index */
	u_int	pr_priority[7] ;	/* priority values */
} ;

/*
 * P0E : PHY elasticity buffer status
 */
#define SMT_P_EB	0x000e		/* PHY EB status */
#define SWAP_SMT_P_EB	"ssl"

struct smt_p_eb {
	struct smt_para	para ;		/* generic parameter header */
	u_short	eb_mib_index ;		/* MIB index */
	u_short	eb_index ;		/* phy index */
	u_int	eb_error_ct ;		/* # of eb overflows */
} ;

/*
 * P0F : manufacturer field
 */
#define SMT_P_MANUFACTURER	0x000f	/* manufacturer field */
#define SWAP_SMT_P_MANUFACTURER	""

struct smp_p_manufacturer {
	struct smt_para	para ;		/* generic parameter header */
	u_char mf_data[32] ;		/* OUI + arbitrary data */
} ;

/*
 * P10 : user field
 */
#define SMT_P_USER		0x0010	/* manufacturer field */
#define SWAP_SMT_P_USER	""

struct smp_p_user {
	struct smt_para	para ;		/* generic parameter header */
	u_char us_data[32] ;		/* arbitrary data */
} ;



/*
 * P11 : echo data
 */
#define SMT_P_ECHODATA	0x0011		/* echo data */
#define SWAP_SMT_P_ECHODATA	""

struct smt_p_echo {
	struct smt_para	para ;		/* generic parameter header */
	u_char	ec_data[SMT_MAX_ECHO_LEN-4] ;	/* echo data */
} ;

/*
 * P12 : reason code
 */
#define SMT_P_REASON	0x0012		/* reason code */
#define SWAP_SMT_P_REASON	"l"

struct smt_p_reason {
	struct smt_para	para ;		/* generic parameter header */
	u_int	rdf_reason ;		/* CLASS/VERSION */
} ;
#define SMT_RDF_CLASS	0x00000001	/* class not supported */
#define SMT_RDF_VERSION	0x00000002	/* version not supported */
#define SMT_RDF_SUCCESS	0x00000003	/* success (PMF) */
#define SMT_RDF_BADSET	0x00000004	/* bad set count (PMF) */
#define SMT_RDF_ILLEGAL 0x00000005	/* read only (PMF) */
#define SMT_RDF_NOPARAM	0x6		/* parameter not supported (PMF) */
#define SMT_RDF_RANGE	0x8		/* out of range */
#define SMT_RDF_AUTHOR	0x9		/* not autohorized */
#define SMT_RDF_LENGTH	0x0a		/* length error */
#define SMT_RDF_TOOLONG	0x0b		/* length error */
#define SMT_RDF_SBA	0x0d		/* SBA denied */

/*
 * P13 : refused frame beginning
 */
#define SMT_P_REFUSED	0x0013		/* refused frame beginning */
#define SWAP_SMT_P_REFUSED	"l"

struct smt_p_refused {
	struct smt_para	para ;		/* generic parameter header */
	u_int	ref_fc ;		/* 3 bytes 0 + FC */
	struct smt_header	ref_header ;	/* refused header */
} ;

/*
 * P14 : supported SMT versions
 */
#define SMT_P_VERSION	0x0014		/* SMT supported versions */
#define SWAP_SMT_P_VERSION	"sccss"

struct smt_p_version {
	struct smt_para	para ;		/* generic parameter header */
	u_short	v_pad ;
	u_char	v_n ;			/* 1 .. 0xff, #versions */
	u_char	v_index ;		/* 1 .. 0xff, index of op. v. */
	u_short	v_version[1] ;		/* list of min. 1 version */
	u_short	v_pad2 ;		/* pad if necessary */
} ;

/*
 * P15 : Resource Type
 */
#define	SWAP_SMT_P0015		"l"

struct smt_p_0015 {
	struct smt_para	para ;		/* generic parameter header */
	u_int		res_type ;	/* recsource type */
} ;

#define	SYNC_BW		0x00000001L	/* Synchronous Bandwidth */

/*
 * P16 : SBA Command
 */
#define	SWAP_SMT_P0016		"l"

struct smt_p_0016 {
	struct smt_para	para ;		/* generic parameter header */
	u_int		sba_cmd ;	/* command for the SBA */
} ;

#define	REQUEST_ALLOCATION	0x1	/* req allocation of sync bandwidth */
#define	REPORT_ALLOCATION	0x2	/* rep of sync bandwidth allocation */
#define	CHANGE_ALLOCATION	0x3	/* forces a station using sync band-*/
					/* width to change its current allo-*/
					/* cation */

/*
 * P17 : SBA Payload Request
 */
#define	SWAP_SMT_P0017		"l"

struct smt_p_0017 {
	struct smt_para	para ;		/* generic parameter header */
	int		sba_pl_req ;	/* total sync bandwidth measured in */
} ;					/* bytes per 125 us */

/*
 * P18 : SBA Overhead Request
 */
#define	SWAP_SMT_P0018		"l"

struct smt_p_0018 {
	struct smt_para	para ;		/* generic parameter header */
	int		sba_ov_req ;	/* total sync bandwidth req for overhead*/
} ;					/* measuered in bytes per T_Neg */

/*
 * P19 : SBA Allocation Address
 */
#define	SWAP_SMT_P0019		"s6"

struct smt_p_0019 {
	struct smt_para	para ;		/* generic parameter header */
	u_short		sba_pad ;
	struct fddi_addr alloc_addr ;	/* Allocation Address */
} ;

/*
 * P1A : SBA Category
 */
#define	SWAP_SMT_P001A		"l"

struct smt_p_001a {
	struct smt_para	para ;		/* generic parameter header */
	u_int		category ;	/* Allocator defined classification */
} ;

/*
 * P1B : Maximum T_Neg
 */
#define	SWAP_SMT_P001B		"l"

struct smt_p_001b {
	struct smt_para	para ;		/* generic parameter header */
	u_int		max_t_neg ;	/* longest T_NEG for the sync service*/
} ;

/*
 * P1C : Minimum SBA Segment Size
 */
#define	SWAP_SMT_P001C		"l"

struct smt_p_001c {
	struct smt_para	para ;		/* generic parameter header */
	u_int		min_seg_siz ;	/* smallest number of bytes per frame*/
} ;

/*
 * P1D : SBA Allocatable
 */
#define	SWAP_SMT_P001D		"l"

struct smt_p_001d {
	struct smt_para	para ;		/* generic parameter header */
	u_int		allocatable ;	/* total sync bw available for alloc */
} ;

/*
 * P20 0B : frame status capabilities
 * NOTE: not in swap table, is used by smt.c AND PMF table
 */
#define SMT_P_FSC	0x200b
/* #define SWAP_SMT_P_FSC	"ssss" */

struct smt_p_fsc {
	struct smt_para	para ;		/* generic parameter header */
	u_short	fsc_pad0 ;
	u_short	fsc_mac_index ;		/* mac index 1 .. ff */
	u_short	fsc_pad1 ;
	u_short	fsc_value ;		/* FSC_TYPE[0-2] */
} ;

#define FSC_TYPE0	0		/* "normal" node (A/C handling) */
#define FSC_TYPE1	1		/* Special A/C indicator forwarding */
#define FSC_TYPE2	2		/* Special A/C indicator forwarding */

/*
 * P00 21 : user defined authoriziation (see pmf.c)
 */
#define SMT_P_AUTHOR	0x0021

/*
 * notification parameters
 */
#define SWAP_SMT_P1048	"ll"
struct smt_p_1048 {
	u_int p1048_flag ;
	u_int p1048_cf_state ;
} ;

/*
 * NOTE: all 2xxx 3xxx and 4xxx must include the INDEX in the swap string,
 *	even so the INDEX is NOT part of the struct.
 *	INDEX is already swapped in pmf.c, format in string is '4'
 */
#define SWAP_SMT_P208C	"4lss66"
struct smt_p_208c {
	u_int			p208c_flag ;
	u_short			p208c_pad ;
	u_short			p208c_dupcondition ;
	struct	fddi_addr	p208c_fddilong ;
	struct	fddi_addr	p208c_fddiunalong ;
} ;

#define SWAP_SMT_P208D	"4lllll"
struct smt_p_208d {
	u_int			p208d_flag ;
	u_int			p208d_frame_ct ;
	u_int			p208d_error_ct ;
	u_int			p208d_lost_ct ;
	u_int			p208d_ratio ;
} ;

#define SWAP_SMT_P208E	"4llll"
struct smt_p_208e {
	u_int			p208e_flag ;
	u_int			p208e_not_copied ;
	u_int			p208e_copied ;
	u_int			p208e_not_copied_ratio ;
} ;

#define SWAP_SMT_P208F	"4ll6666s6"

struct smt_p_208f {
	u_int			p208f_multiple ;
	u_int			p208f_nacondition ;
	struct fddi_addr	p208f_old_una ;
	struct fddi_addr	p208f_new_una ;
	struct fddi_addr	p208f_old_dna ;
	struct fddi_addr	p208f_new_dna ;
	u_short			p208f_curren_path ;
	struct fddi_addr	p208f_smt_address ;
} ;

#define SWAP_SMT_P2090	"4lssl"

struct smt_p_2090 {
	u_int			p2090_multiple ;
	u_short			p2090_availablepaths ;
	u_short			p2090_currentpath ;
	u_int			p2090_requestedpaths ;
} ;

/*
 * NOTE:
 * special kludge for parameters 320b,320f,3210
 * these parameters are part of RAF frames
 * RAF frames are parsed in SBA.C and must be swapped
 * PMF.C has special code to avoid double swapping
 */
#ifdef	LITTLE_ENDIAN
#define SBAPATHINDEX	(0x01000000L)
#else
#define SBAPATHINDEX	(0x01L)
#endif

#define	SWAP_SMT_P320B	"42s"

struct	smt_p_320b {
	struct smt_para para ;	/* generic parameter header */
	u_int	mib_index ;
	u_short path_pad ;
	u_short	path_index ;
} ;

#define	SWAP_SMT_P320F	"4l"

struct	smt_p_320f {
	struct smt_para para ;	/* generic parameter header */
	u_int	mib_index ;
	u_int	mib_payload ;
} ;

#define	SWAP_SMT_P3210	"4l"

struct	smt_p_3210 {
	struct smt_para para ;	/* generic parameter header */
	u_int	mib_index ;
	u_int	mib_overhead ;
} ;

#define SWAP_SMT_P4050	"4l1111ll"

struct smt_p_4050 {
	u_int			p4050_flag ;
	u_char			p4050_pad ;
	u_char			p4050_cutoff ;
	u_char			p4050_alarm ;
	u_char			p4050_estimate ;
	u_int			p4050_reject_ct ;
	u_int			p4050_ct ;
} ;

#define SWAP_SMT_P4051	"4lssss"
struct smt_p_4051 {
	u_int			p4051_multiple ;
	u_short			p4051_porttype ;
	u_short			p4051_connectstate ;
	u_short			p4051_pc_neighbor ;
	u_short			p4051_pc_withhold ;
} ;

#define SWAP_SMT_P4052	"4ll"
struct smt_p_4052 {
	u_int			p4052_flag ;
	u_int			p4052_eberrorcount ;
} ;

#define SWAP_SMT_P4053	"4lsslss"

struct smt_p_4053 {
	u_int			p4053_multiple ;
	u_short			p4053_availablepaths ;
	u_short			p4053_currentpath ;
	u_int			p4053_requestedpaths ;
	u_short			p4053_mytype ;
	u_short			p4053_neighbortype ;
} ;


#define SMT_P_SETCOUNT	0x1035
#define SWAP_SMT_P_SETCOUNT	"l8"

struct smt_p_setcount {
	struct smt_para	para ;		/* generic parameter header */
	u_int		count ;
	u_char		timestamp[8] ;
} ;

/*
 * SMT FRAMES
 */

/*
 * NIF : neighbor information frames
 */
struct smt_nif {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_una	una ;		/* UNA */
	struct smt_p_sde	sde ;		/* station descriptor */
	struct smt_p_state	state ;		/* station state */
#ifdef	SMT6_10
	struct smt_p_fsc	fsc ;		/* frame status cap. */
#endif
} ;

/*
 * SIF : station information frames
 */
struct smt_sif_config {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_timestamp	ts ;		/* time stamp */
	struct smt_p_sde	sde ;		/* station descriptor */
	struct smt_p_version	version ;	/* supported versions */
	struct smt_p_state	state ;		/* station state */
	struct smt_p_policy	policy ;	/* station policy */
	struct smt_p_latency	latency ;	/* path latency */
	struct smt_p_neighbor	neighbor ;	/* neighbors, we have only one*/
#ifdef	OPT_PMF
	struct smt_p_setcount	setcount ;	 /* Set Count mandatory */
#endif
	/* WARNING : path MUST BE LAST FIELD !!! (see smt.c:smt_fill_path) */
	struct smt_p_path	path ;		/* path descriptor */
} ;
#define SIZEOF_SMT_SIF_CONFIG	(sizeof(struct smt_sif_config)- \
				 sizeof(struct smt_p_path))

struct smt_sif_operation {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_timestamp	ts ;		/* time stamp */
	struct smt_p_mac_status	status ;	/* mac status */
	struct smt_p_mac_counter mc ;		/* MAC counter */
	struct smt_p_mac_fnc 	fnc ;		/* MAC frame not copied */
	struct smp_p_manufacturer man ;		/* manufacturer field */
	struct smp_p_user	user ;		/* user field */
#ifdef	OPT_PMF
	struct smt_p_setcount	setcount ;	 /* Set Count mandatory */
#endif
	/* must be last */
	struct smt_p_lem	lem[1] ;	/* phy lem status */
} ;
#define SIZEOF_SMT_SIF_OPERATION	(sizeof(struct smt_sif_operation)- \
					 sizeof(struct smt_p_lem))

/*
 * ECF : echo frame
 */
struct smt_ecf {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_echo	ec_echo ;	/* echo parameter */
} ;
#define SMT_ECF_LEN	(sizeof(struct smt_header)+sizeof(struct smt_para))

/*
 * RDF : request denied frame
 */
struct smt_rdf {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_reason	reason ;	/* reason code */
	struct smt_p_version	version ;	/* supported versions */
	struct smt_p_refused	refused ;	/* refused frame fragment */
} ;

/*
 * SBA Request Allocation Responce Frame
 */
struct smt_sba_alc_res {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_0015	s_type ;	/* resource type */
	struct smt_p_0016	cmd ;		/* SBA command */
	struct smt_p_reason	reason ;	/* reason code */
	struct smt_p_320b	path ;		/* path type */
	struct smt_p_320f	payload ;	/* current SBA payload */
	struct smt_p_3210	overhead ;	/* current SBA overhead */
	struct smt_p_0019	a_addr ;	/* Allocation Address */
	struct smt_p_001a	cat ;		/* Category - from the request */
	struct smt_p_001d	alloc ;		/* SBA Allocatable */
} ;

/*
 * SBA Request Allocation Request Frame
 */
struct smt_sba_alc_req {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_0015	s_type ;	/* resource type */
	struct smt_p_0016	cmd ;		/* SBA command */
	struct smt_p_320b	path ;		/* path type */
	struct smt_p_0017	pl_req ;	/* requested payload */
	struct smt_p_0018	ov_req ;	/* requested SBA overhead */
	struct smt_p_320f	payload ;	/* current SBA payload */
	struct smt_p_3210	overhead ;	/* current SBA overhead */
	struct smt_p_0019	a_addr ;	/* Allocation Address */
	struct smt_p_001a	cat ;		/* Category - from the request */
	struct smt_p_001b	tneg ;		/* max T-NEG */
	struct smt_p_001c	segm ;		/* minimum segment size */
} ;

/*
 * SBA Change Allocation Request Frame
 */
struct smt_sba_chg {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_0015	s_type ;	/* resource type */
	struct smt_p_0016	cmd ;		/* SBA command */
	struct smt_p_320b	path ;		/* path type */
	struct smt_p_320f	payload ;	/* current SBA payload */
	struct smt_p_3210	overhead ;	/* current SBA overhead */
	struct smt_p_001a	cat ;		/* Category - from the request */
} ;

/*
 * SBA Report Allocation Request Frame
 */
struct smt_sba_rep_req {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_0015	s_type ;	/* resource type */
	struct smt_p_0016	cmd ;		/* SBA command */
} ;

/*
 * SBA Report Allocation Response Frame
 */
struct smt_sba_rep_res {
	struct smt_header	smt ;		/* generic header */
	struct smt_p_0015	s_type ;	/* resource type */
	struct smt_p_0016	cmd ;		/* SBA command */
	struct smt_p_320b	path ;		/* path type */
	struct smt_p_320f	payload ;	/* current SBA payload */
	struct smt_p_3210	overhead ;	/* current SBA overhead */
} ;

/*
 * actions
 */
#define SMT_STATION_ACTION	1
#define SMT_STATION_ACTION_CONNECT	0
#define SMT_STATION_ACTION_DISCONNECT	1
#define SMT_STATION_ACTION_PATHTEST	2
#define SMT_STATION_ACTION_SELFTEST	3
#define SMT_STATION_ACTION_DISABLE_A	4
#define SMT_STATION_ACTION_DISABLE_B	5
#define SMT_STATION_ACTION_DISABLE_M	6

#define SMT_PORT_ACTION		2
#define SMT_PORT_ACTION_MAINT	0
#define SMT_PORT_ACTION_ENABLE	1
#define SMT_PORT_ACTION_DISABLE	2
#define SMT_PORT_ACTION_START	3
#define SMT_PORT_ACTION_STOP	4

#endif	/* _SMT_ */
