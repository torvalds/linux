
#ifndef MDD_H
#define MDD_H 1

/*************************************************************************************************************
*
* FILE		: mdd.h
*
* DATE		: $Date: 2004/08/05 11:47:10 $   $Revision: 1.6 $
* Original		: 2004/05/25 05:59:37    Revision: 1.57      Tag: hcf7_t20040602_01
* Original		: 2004/05/13 15:31:45    Revision: 1.54      Tag: hcf7_t7_20040513_01
* Original		: 2004/04/15 09:24:41    Revision: 1.47      Tag: hcf7_t7_20040415_01
* Original		: 2004/04/13 14:22:45    Revision: 1.46      Tag: t7_20040413_01
* Original		: 2004/04/01 15:32:55    Revision: 1.42      Tag: t7_20040401_01
* Original		: 2004/03/10 15:39:28    Revision: 1.38      Tag: t20040310_01
* Original		: 2004/03/04 11:03:37    Revision: 1.36      Tag: t20040304_01
* Original		: 2004/03/02 09:27:11    Revision: 1.34      Tag: t20040302_03
* Original		: 2004/02/24 13:00:27    Revision: 1.29      Tag: t20040224_01
* Original		: 2004/02/18 17:13:57    Revision: 1.26      Tag: t20040219_01
*
* AUTHOR	: Nico Valster
*
* DESC		: Definitions and Prototypes for HCF, DHF, MMD and MSF
*
***************************************************************************************************************
*
*
* SOFTWARE LICENSE
*
* This software is provided subject to the following terms and conditions,
* which you should read carefully before using the software.  Using this
* software indicates your acceptance of these terms and conditions.  If you do
* not agree with these terms and conditions, do not use the software.
*
* COPYRIGHT © 1994 - 1995	by AT&T.				All Rights Reserved
* COPYRIGHT © 1996 - 2000 by Lucent Technologies.	All Rights Reserved
* COPYRIGHT © 2001 - 2004	by Agere Systems Inc.	All Rights Reserved
* All rights reserved.
*
* Redistribution and use in source or binary forms, with or without
* modifications, are permitted provided that the following conditions are met:
*
* . Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following Disclaimer as comments in the code as
*    well as in the documentation and/or other materials provided with the
*    distribution.
*
* . Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following Disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* . Neither the name of Agere Systems Inc. nor the names of the contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* Disclaimer
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
* INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
* USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
* RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
* DAMAGE.
*
*
************************************************************************************************************/


/************************************************************************************************************
*
* The macros Xn(...) and XXn(...) are used to define the LTV's (short for Length Type Value[ ]) ,
* aka RIDs, processed by the Hermes.
* The n in Xn and XXn reflects the number of "Value" fields in these RIDs.
*
* Xn(...) : Macros used for RIDs which use only type hcf_16 for the "V" fields of the LTV.
* Xn takes as parameters a RID name and "n" name(s), one for each of the "V" fields of the LTV.
*
* XXn(...) : Macros used for RIDs which use at least one other type then hcf_16 for a "V" field
* of the LTV.
* XXn(..) takes as parameters a RID name and "n" pair(s) of type and name, one for each "V" field
* of the LTV

 ******************************************  e x a m p l e s  ***********************************************

* X1(RID_NAME, parameters...) : expands to :
*    typedef struct RID_NAME_STRCT {
*         hcf_16  len;
*         hcf_16  typ;
*         hcf_16  par1;
*    } RID_NAME_STRCT;

* X2(RID_NAME, parameters...) : expands to :
*    typedef struct RID_NAME_STRCT {
*         hcf_16  len;
*         hcf_16  typ;
*         hcf_16  par1;
*         hcf_16  par2;
*    } RID_NAME_STRCT;


* XX1(RID_NAME, par1type, par1name, ...) : expands to :
*    typedef struct RID_NAME_STRCT {
*       hcf_16    len;
*       hcf_16    typ;
*       par1type  par1name;
*    } RID_NAME_STRCT;

************************************************************************************************************/

/******************************* XX Sub-macro definitions **************************************************/

#define XX1( name, type1, par1 )	\
typedef struct {				  	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	type1	par1;               	\
} name##_STRCT;

#define XX2( name, type1, par1, type2, par2 )	\
typedef struct {				   	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	type1	par1;               	\
	type2	par2;               	\
} name##_STRCT;

#define XX3( name, type1, par1, type2, par2, type3, par3 )	\
typedef struct name##_STRCT {   	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	type1	par1;               	\
	type2	par2;               	\
	type3	par3;               	\
} name##_STRCT;

#define XX4( name, type1, par1, type2, par2, type3, par3, type4, par4 )	\
typedef struct {				  	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	type1	par1;               	\
	type2	par2;               	\
	type3	par3;               	\
	type4	par4;               	\
} name##_STRCT;

#define X1( name, par1 )	\
typedef struct name##_STRCT {   	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	hcf_16	par1;               	\
} name##_STRCT;

#define X2( name, par1, par2 )		\
typedef struct {			    	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	hcf_16	par1;               	\
	hcf_16	par2;               	\
} name##_STRCT;

#define X3( name, par1, par2, par3 )		\
typedef struct {			    	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	hcf_16	par1;               	\
	hcf_16	par2;               	\
	hcf_16	par3;               	\
} name##_STRCT;

#define X4( name, par1, par2, par3, par4 )		\
typedef struct {			    	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	hcf_16	par1;               	\
	hcf_16	par2;               	\
	hcf_16	par3;               	\
	hcf_16	par4;               	\
} name##_STRCT;

#define X5( name, par1, par2, par3, par4, par5 )		\
typedef struct {			    	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	hcf_16	par1;               	\
	hcf_16	par2;               	\
	hcf_16	par3;               	\
	hcf_16	par4;               	\
	hcf_16	par5;               	\
} name##_STRCT;

#define X6( name, par1, par2, par3, par4, par5, par6 )		\
typedef struct {			    	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	hcf_16	par1;               	\
	hcf_16	par2;               	\
	hcf_16	par3;               	\
	hcf_16	par4;               	\
	hcf_16	par5;               	\
	hcf_16	par6;               	\
} name##_STRCT;

#define X8( name, par1, par2, par3, par4, par5, par6, par7, par8 )		\
typedef struct {			    	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	hcf_16	par1;               	\
	hcf_16	par2;               	\
	hcf_16	par3;               	\
	hcf_16	par4;               	\
	hcf_16	par5;               	\
	hcf_16	par6;               	\
	hcf_16	par7;               	\
	hcf_16	par8;               	\
} name##_STRCT;

#define X11( name, par1, par2, par3, par4, par5, par6, par7, par8, par9, par10, par11 )		\
typedef struct {			    	\
	hcf_16	len;                	\
	hcf_16	typ;                	\
	hcf_16	par1;               	\
	hcf_16	par2;               	\
	hcf_16	par3;               	\
	hcf_16	par4;               	\
	hcf_16	par5;               	\
	hcf_16	par6;               	\
	hcf_16	par7;               	\
	hcf_16	par8;               	\
	hcf_16	par9;               	\
	hcf_16	par10;               	\
	hcf_16	par11;               	\
} name##_STRCT;

/******************************* Substructure definitions **************************************************/

//apparently not needed (CFG_CNF_COUNTRY)
typedef struct CHANNEL_SET {				//channel set structure used in the CFG_CNF_COUNTRY LTV
	hcf_16	first_channel;
	hcf_16	number_of_channels;
	hcf_16	max_tx_output_level;
} CHANNEL_SET;

typedef struct KEY_STRCT {					// key structure used in the CFG_DEFAULT_KEYS LTV
    hcf_16  len;	              				//length of key
    hcf_8   key[14];							//encryption key
} KEY_STRCT;

typedef struct SCAN_RS_STRCT {				// Scan Result structure used in the CFG_SCAN LTV
	hcf_16	channel_id;
	hcf_16	noise_level;
	hcf_16	signal_level;
	hcf_8	bssid[6];
	hcf_16	beacon_interval_time;
	hcf_16	capability;
	hcf_16	ssid_len;
	hcf_8	ssid_val[32];
} SCAN_RS_STRCT;

typedef struct CFG_RANGE_SPEC_STRCT {		// range specification structure used in CFG_RANGES, CFG_RANGE1 etc
	hcf_16	variant;
	hcf_16	bottom;
	hcf_16	top;
} CFG_RANGE_SPEC_STRCT;

typedef struct CFG_RANGE_SPEC_BYTE_STRCT {	// byte oriented range specification structure used in CFG_RANGE_B LTV
	hcf_8	variant[2];
	hcf_8	bottom[2];
	hcf_8	top[2];
} CFG_RANGE_SPEC_BYTE_STRCT;

//used to set up "T" functionality for Info frames, i.e. log info frames in MSF supplied buffer and MailBox
XX1( RID_LOG, unsigned short FAR*, bufp )
typedef RID_LOG_STRCT  FAR *RID_LOGP;
XX1( CFG_RID_LOG, RID_LOGP, recordp )

 X1( LTV,		val[1] )												/*minimum LTV proto typ	*/
 X1( LTV_MAX,	val[HCF_MAX_LTV] )										/*maximum LTV proto typ	*/
XX2( CFG_REG_MB, hcf_16* , mb_addr, hcf_16, mb_size )

typedef struct CFG_MB_INFO_FRAG {	// specification of buffer fragment
	unsigned short FAR*	frag_addr;
	hcf_16				frag_len;
} CFG_MB_INFO_FRAG;

/* Mail Box Info Block structures,
 * the base form: CFG_MB_INFO_STRCT
 * and the derived forms: CFG_MB_INFO_RANGE<n>_STRCT with n is 1, 2, 3 or 20
 * predefined for a payload of 1, and up to 2, 3 and 20 CFG_MB_INFO_FRAG elements */
XX3( CFG_MB_INFO,		  hcf_16, base_typ, hcf_16, frag_cnt, CFG_MB_INFO_FRAG, frag_buf[ 1] )
XX3( CFG_MB_INFO_RANGE1,  hcf_16, base_typ, hcf_16, frag_cnt, CFG_MB_INFO_FRAG, frag_buf[ 1] )
XX3( CFG_MB_INFO_RANGE2,  hcf_16, base_typ, hcf_16, frag_cnt, CFG_MB_INFO_FRAG, frag_buf[ 2] )
XX3( CFG_MB_INFO_RANGE3,  hcf_16, base_typ, hcf_16, frag_cnt, CFG_MB_INFO_FRAG, frag_buf[ 3] )
XX3( CFG_MB_INFO_RANGE20, hcf_16, base_typ, hcf_16, frag_cnt, CFG_MB_INFO_FRAG, frag_buf[20] )

XX3( CFG_MB_ASSERT, hcf_16, line, hcf_16, trace, hcf_32, qualifier )	/*MBInfoBlock for asserts	*/
#if (HCF_ASSERT) & ( HCF_ASSERT_LNK_MSF_RTN | HCF_ASSERT_RT_MSF_RTN )
typedef void (MSF_ASSERT_RTN)( unsigned int , hcf_16, hcf_32 );
typedef MSF_ASSERT_RTN /*can't link FAR*/ * MSF_ASSERT_RTNP;
/* CFG_REG_ASSERT_RTNP (0x0832)	(de-)register MSF Callback routines
 * lvl:  Assert level filtering (not yet implemented)
 * rtnp: address of MSF_ASSERT_RTN (native Endian format) */
XX2( CFG_REG_ASSERT_RTNP, hcf_16, lvl, MSF_ASSERT_RTNP, rtnp )
#endif // HCF_ASSERT_LNK_MSF_RTN / HCF_ASSERT_RT_MSF_RTN

 X1( CFG_HCF_OPT, val[20] )											  	/*(Compile time) options	*/
 X3( CFG_CMD_HCF, cmd, mode, add_info )									/*HCF Engineering command	*/

typedef struct {
	hcf_16		len;
	hcf_16		typ;
	hcf_16		mode;			// PROG_STOP/VOLATILE [FLASH/SEEPROM/SEEPROM_READBACK]
	hcf_16		segment_size;  	// size of the segment in bytes
	hcf_32		nic_addr;  		// destination address (in NIC memory)
	hcf_16		flags;			// 0x0001	: CRC Yes/No
//	hcf_32		flags;			// 0x0001	: CRC Yes/No
	/* ;? still not the whole story
	 * flags is extended from 16 to 32 bits to force that compiling FW.C produces the same structures
	 * in memory as FUPU4 BIN files.
	 * Note that the problem arises from the violation of the constraint to use packing at byte boundaries
	 * as was stipulated in the WCI-specification
	 * The Pack pragma can't resolve this issue, because that impacts all members of the structure with
	 * disregard of their actual size, so aligning host_addr under MSVC 1.5 at 4 bytes, also aligns
	 * len, typ etc on 4 bytes
	 * */
//	hcf_16		pad; 	 		//!! be careful alignment problems for Bin download versus C download
	hcf_8 FAR   *host_addr;  	// source address (in Host memory)
} CFG_PROG_STRCT; // segment_descp;

// a structure used for transporting debug-related information from firmware
// via the HCF, into the MSF
typedef struct {
    hcf_16      len;
    hcf_16      typ;
    hcf_16      msg_id, msg_par, msg_tstamp;
} CFG_FW_PRINTF_STRCT;

// a structure used to define the location and size of a certain debug-related
// buffer in nic-ram.
typedef struct {
    hcf_16      len;
    hcf_16      typ;
    hcf_32      DbMsgCount, 	// ds (nicram) address of a counter
                DbMsgBuffer, 	// ds (nicram) address of the buffer
                DbMsgSize, 		// number of entries (each 3 word in size) in this buffer
                DbMsgIntrvl;	// ds (nicram) address of interval for generating InfDrop event
} CFG_FW_PRINTF_BUFFER_LOCATION_STRCT;

XX3( CFG_RANGES,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[ 1] ) /*Actor/Supplier range (1 variant)*/
XX3( CFG_RANGE1,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[ 1] ) /*Actor/Supplier range (1 variant)*/
XX3( CFG_RANGE2,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[ 2] ) /*Actor range ( 2 variants)		*/
XX3( CFG_RANGE3,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[ 3] ) /*Actor range ( 3 variants)		*/
XX3( CFG_RANGE4,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[ 4] ) /*Actor range ( 4 variants)		*/
XX3( CFG_RANGE5,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[ 5] ) /*Actor range ( 5 variants)		*/
XX3( CFG_RANGE6,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[ 6] ) /*Actor range ( 6 variants)		*/
XX3( CFG_RANGE7,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[ 7] ) /*Actor range ( 7 variants)		*/
XX3( CFG_RANGE20,	hcf_16, role, hcf_16, id, CFG_RANGE_SPEC_STRCT, var_rec[20] ) /*Actor range (20 variants)		*/

/*Frames */
 X3( CFG_ASSOC_STAT,  assoc_stat, station_addr[3], val[46] ) 	/*Association status, basic					*/
 X2( CFG_ASSOC_STAT3, assoc_stat, station_addr[3] ) 								/*assoc_stat:3			*/
 X3( CFG_ASSOC_STAT1, assoc_stat, station_addr[3], frame_body[43] )					/*assoc_stat:1			*/
 X4( CFG_ASSOC_STAT2, assoc_stat, station_addr[3], old_ap_addr[3], frame_body[43] )	/*assoc_stat:2			*/

/*Static Configurations */
 X1( CFG_CNF_PORT_TYPE,				port_type			 ) /*[STA] Connection control characteristics				*/
 X1( CFG_MAC_ADDR,					mac_addr[3] 		 ) /*general: FC01,FC08,FC11,FC12,FC13,FC14,FC15,FC16 		*/
 X1( CFG_CNF_OWN_MAC_ADDR,			mac_addr[3]			 )
 X1( CFG_ID,						ssid[17]			 ) /*0xFC02, 0xFC04, 0xFC0E 								*/
/*	X1( CFG_DESIRED_SSID,			ssid[17]			 )	see Dynamic Configurations								*/
 X1( CFG_CNF_OWN_CHANNEL,			channel				 ) /*Communication channel for BSS creation					*/
 X1( CFG_CNF_OWN_SSID,				ssid[17]			 )
 X1( CFG_CNF_OWN_ATIM_WINDOW,		atim_window			 )
 X1( CFG_CNF_SYSTEM_SCALE,			system_scale		 )
 X1( CFG_CNF_MAX_DATA_LEN,			max_data_len		 )
 X1( CFG_CNF_WDS_ADDR,				mac_addr[3]			 ) /*[STA] MAC Address of corresponding WDS Link node		*/
 X1( CFG_CNF_PM_ENABLED,			pm_enabled			 ) /*[STA] Switch for ESS Power Management (PM) On/Off		*/
 X1( CFG_CNF_PM_EPS,				pm_eps				 ) /*[STA] Switch for ESS PM EPS/PS Mode					*/
 X1( CFG_CNF_MCAST_RX,				mcast_rx			 ) /*[STA] Switch for ESS PM Multicast reception On/Off		*/
 X1( CFG_CNF_MAX_SLEEP_DURATION,	duration			 ) /*[STA] Maximum sleep time for ESS PM					*/
 X1( CFG_CNF_PM_HOLDOVER_DURATION,	duration			 ) /*[STA] Holdover time for ESS PM							*/
 X1( CFG_CNF_OWN_NAME,				ssid[17]			 ) /*Identification text for diagnostic purposes			*/
 X1( CFG_CNF_OWN_DTIM_PERIOD,		period				 ) /*[AP] Beacon intervals between successive DTIMs			*/
 X1( CFG_CNF_WDS_ADDR1,				mac_addr[3]			 ) /*[AP] Port 1 MAC Adrs of corresponding WDS Link node	*/
 X1( CFG_CNF_WDS_ADDR2,				mac_addr[3]			 ) /*[AP] Port 2 MAC Adrs of corresponding WDS Link node	*/
 X1( CFG_CNF_WDS_ADDR3,				mac_addr[3]			 ) /*[AP] Port 3 MAC Adrs of corresponding WDS Link node	*/
 X1( CFG_CNF_WDS_ADDR4,				mac_addr[3]			 ) /*[AP] Port 4 MAC Adrs of corresponding WDS Link node	*/
 X1( CFG_CNF_WDS_ADDR5,				mac_addr[3]			 ) /*[AP] Port 5 MAC Adrs of corresponding WDS Link node	*/
 X1( CFG_CNF_WDS_ADDR6,				mac_addr[3]			 ) /*[AP] Port 6 MAC Adrs of corresponding WDS Link node	*/
 X1( CFG_CNF_MCAST_PM_BUF,			mcast_pm_buf		 ) /*[AP] Switch for PM buffering of Multicast Messages	*/
 X1( CFG_CNF_REJECT_ANY,			reject_any			 ) /*[AP] Switch for PM buffering of Multicast Messages	*/
//X1( CFG_CNF_ENCRYPTION_ENABLED,	encryption			 ) /*specify encryption type of Tx/Rx messages				*/
 X1( CFG_CNF_ENCRYPTION,			encryption			 ) /*specify encryption type of Tx/Rx messages				*/
 X1( CFG_CNF_AUTHENTICATION,		authentication		 ) /*selects Authentication algorithm						*/
 X1( CFG_CNF_EXCL_UNENCRYPTED,		exclude_unencrypted	 ) /*[AP] Switch for 'clear-text' rx message acceptance		*/
 X1( CFG_CNF_MCAST_RATE,			mcast_rate			 ) /*Transmit Data rate for Multicast frames				*/
 X1( CFG_CNF_INTRA_BSS_RELAY,		intra_bss_relay		 ) /*[AP] Switch for IntraBBS relay							*/
 X1( CFG_CNF_MICRO_WAVE,			micro_wave			 ) /*MicroWave (Robustness)									*/
 X1( CFG_CNF_LOAD_BALANCING,		load_balancing		 ) /*Load Balancing	  (Boolean, 0=OFF, 1=ON, default=1)		*/
 X1( CFG_CNF_MEDIUM_DISTRIBUTION,	medium_distribution	 ) /*Medium Distribution (Boolean, 0=OFF, 1=ON, default=1)	*/
 X1( CFG_CNF_GROUP_ADDR_FILTER,		group_addr_filter	 ) /*Group Address Filter								   	*/
 X1( CFG_CNF_TX_POW_LVL,			tx_pow_lvl			 ) /*Tx Power Level										   	*/
XX4( CFG_CNF_COUNTRY_INFO,								 \
		hcf_16, n_channel_sets, hcf_16, country_code[2], \
		hcf_16, environment, CHANNEL_SET, channel_set[1] ) /*Current Country Info  									*/
XX4( CFG_CNF_COUNTRY_INFO_MAX,							 \
		hcf_16, n_channel_sets, hcf_16, country_code[2], \
		hcf_16, environment, CHANNEL_SET, channel_set[14]) /*Current Country Info  									*/

/*Dynamic Configurations */
 X1( CFG_DESIRED_SSID,			ssid[17]					 )	/*[STA] Service Set identification for connection	*/
#define GROUP_ADDR_SIZE			(32 * 6)						//32 6-byte MAC-addresses
 X1( CFG_GROUP_ADDR,			mac_addr[GROUP_ADDR_SIZE/2]	 )	/*[STA] Multicast MAC Addresses for Rx-message		*/
 X1( CFG_CREATE_IBSS,			create_ibss					 )	/*[STA] Switch for IBSS creation On/Off				*/
 X1( CFG_RTS_THRH,				rts_thrh					 )	/*[STA] Frame length used for RTS/CTS handshake		*/
 X1( CFG_TX_RATE_CNTL,			tx_rate_cntl				 )	/*[STA] Data rate control for message transmission	*/
 X1( CFG_PROMISCUOUS_MODE,		promiscuous_mode			 )	/*[STA] Switch for Promiscuous mode reception On/Of	*/
 X1( CFG_WOL,					wake_on_lan					 )	/*[STA] Switch for Wake-On-LAN mode					*/
 X1( CFG_RTS_THRH0,				rts_thrh					 )	/*[AP] Port 0 frame length for RTS/CTS handshake	*/
 X1( CFG_RTS_THRH1,				rts_thrh					 )	/*[AP] Port 1 frame length for RTS/CTS handshake	*/
 X1( CFG_RTS_THRH2,				rts_thrh					 )	/*[AP] Port 2 frame length for RTS/CTS handshake	*/
 X1( CFG_RTS_THRH3,				rts_thrh					 )	/*[AP] Port 3 frame length for RTS/CTS handshake	*/
 X1( CFG_RTS_THRH4,				rts_thrh					 )	/*[AP] Port 4 frame length for RTS/CTS handshake	*/
 X1( CFG_RTS_THRH5,				rts_thrh					 )	/*[AP] Port 5 frame length for RTS/CTS handshake	*/
 X1( CFG_RTS_THRH6,				rts_thrh					 )	/*[AP] Port 6 frame length for RTS/CTS handshake	*/
 X1( CFG_TX_RATE_CNTL0,			rate_cntl 					 )	/*[AP] Port 0 data rate control for transmission	*/
 X1( CFG_TX_RATE_CNTL1,			rate_cntl					 )	/*[AP] Port 1 data rate control for transmission	*/
 X1( CFG_TX_RATE_CNTL2,			rate_cntl					 )	/*[AP] Port 2 data rate control for transmission	*/
 X1( CFG_TX_RATE_CNTL3,			rate_cntl					 )	/*[AP] Port 3 data rate control for transmission	*/
 X1( CFG_TX_RATE_CNTL4,			rate_cntl					 )	/*[AP] Port 4 data rate control for transmission	*/
 X1( CFG_TX_RATE_CNTL5,			rate_cntl					 )	/*[AP] Port 5 data rate control for transmission	*/
 X1( CFG_TX_RATE_CNTL6,			rate_cntl					 )	/*[AP] Port 6 data rate control for transmission	*/
XX1( CFG_DEFAULT_KEYS,			KEY_STRCT, key[4]			 )	/*defines set of encryption keys					*/
 X1( CFG_TX_KEY_ID,				tx_key_id					 )	/*select key for encryption of Tx messages			*/
 X1( CFG_SCAN_SSID,				ssid[17]					 )	/*identification for connection						*/
 X5( CFG_ADD_TKIP_DEFAULT_KEY,								 \
		 tkip_key_id_info, tkip_key_iv_info[4], tkip_key[8], \
		 tx_mic_key[4], rx_mic_key[4] 						 )	/*										       		*/
 X6( CFG_ADD_TKIP_MAPPED_KEY,	bssid[3], tkip_key[8], 		 \
		 tsc[4], rsc[4], tx_mic_key[4], rx_mic_key[4] 		 )	/*										       		*/
 X1( CFG_SET_WPA_AUTHENTICATION_SUITE, 						 \
		 ssn_authentication_suite							 )	/*											   		*/
 X1( CFG_REMOVE_TKIP_DEFAULT_KEY,tkip_key_id				 )	/*											   		*/
 X1( CFG_TICK_TIME,				tick_time					 )	/*Auxiliary Timer tick interval						*/
 X1( CFG_DDS_TICK_TIME,			tick_time					 )	/*Disconnected DeepSleep Timer tick interval		*/

/**********************************************************************
* Added for Pattern-matching WakeOnLan. (See firmware design note WMDN281C)
**********************************************************************/
#define WOL_PATTERNS				5		// maximum of 5 patterns in firmware
#define WOL_PATTERN_LEN				124		// maximum 124 bytes pattern length per pattern in firmware
#define WOL_MASK_LEN			 	30		// maximum 30 bytes mask length per pattern in firmware
#define WOL_BUF_SIZE	(WOL_PATTERNS * (WOL_PATTERN_LEN + WOL_MASK_LEN + 6) / 2)
X2( CFG_WOL_PATTERNS, nPatterns, buffer[WOL_BUF_SIZE]		 )  /*[STA] WakeOnLan pattern match, room for 5 patterns*/

 X5( CFG_SUP_RANGE,		role, id, variant, bottom, top				   ) /*[PRI] Primary Supplier compatibility range		*/
/* NIC Information */
 X4( CFG_IDENTITY,			comp_id, variant, version_major, version_minor ) /*identification Prototype							*/
#define CFG_DRV_IDENTITY_STRCT	CFG_IDENTITY_STRCT
#define CFG_PRI_IDENTITY_STRCT	CFG_IDENTITY_STRCT
#define CFG_NIC_IDENTITY_STRCT	CFG_IDENTITY_STRCT
#define CFG_FW_IDENTITY_STRCT	CFG_IDENTITY_STRCT
 X1( CFG_RID_INF_MIN,		y											   ) /*lowest value representing an Information RID		*/
 X1( CFG_MAX_LOAD_TIME,		max_load_time								   ) /*[PRI] Max response time of the Download command	*/
 X3( CFG_DL_BUF,			buf_page, buf_offset, buf_len				   ) /*[PRI] Download buffer location and size			*/
// X5( CFG_PRI_SUP_RANGE,		role, id, variant, bottom, top				   ) /*[PRI] Primary Supplier compatibility range		*/
 X5( CFG_CFI_ACT_RANGES_PRI,role, id, variant, bottom, top				   ) /*[PRI] Controller Actor compatibility ranges		*/
// X5( CFG_NIC_HSI_SUP_RANGE,	role, id, variant, bottom, top				   ) /*H/W - S/W I/F supplier range						*/
 X1( CFG_NIC_SERIAL_NUMBER,	serial_number[17]							   ) /*[PRI] Network I/F Card serial number				*/
 X5( CFG_NIC_MFI_SUP_RANGE,	role, id, variant, bottom, top				   ) /*[PRI] Modem I/F Supplier compatibility range		*/
 X5( CFG_NIC_CFI_SUP_RANGE,	role, id, variant, bottom, top				   ) /*[PRI] Controller I/F Supplier compatibility range*/
//H-I X1( CFG_CHANNEL_LIST,		channel_list								   ) /*Allowed communication channels					*/
//H-I XX2( CFG_REG_DOMAINS,		hcf_16, num_domain, hcf_8, reg_domains[10]	   ) /*List of intended regulatory domains				*/
 X1( CFG_NIC_TEMP_TYPE,		temp_type									   ) /*Hardware temperature range code					*/
//H-I X1( CFG_CIS,				cis[240]									   ) /*PC Card Standard Card Information Structure		*/
 X5( CFG_NIC_PROFILE,													   \
		 profile_code, capability_options, allowed_data_rates, val4, val5  ) /*Card Profile										*/
// X5( CFG_FW_SUP_RANGE,		role, id, variant, bottom, top				   ) /*[STA] Station I/F Supplier compatibility range	*/
 X5( CFG_MFI_ACT_RANGES,	role, id, variant, bottom, top				   ) /*[STA] Modem I/F Actor compatibility ranges		*/
 X5( CFG_CFI_ACT_RANGES_STA,role, id, variant, bottom, top				   ) /*[STA] Controller I/F Actor compatibility ranges	*/
 X5( CFG_MFI_ACT_RANGES_STA,role, id, variant, bottom, top				   ) /*[STA] Controller I/F Actor compatibility ranges	*/
 X1( CFG_NIC_BUS_TYPE,		nic_bus_type								   ) /*NIC bustype derived from BUSSEL host I/F signals */

/*	MAC INFORMATION	*/
 X1( CFG_PORT_STAT,				port_stat							 ) /*[STA] Actual MAC Port connection control status		*/
 X1( CFG_CUR_SSID,				ssid[17]							 ) /*[STA] Identification of the actually connected SS		*/
 X1( CFG_CUR_BSSID,				mac_addr[3]							 ) /*[STA] Identification of the actually connected BSS		*/
 X3( CFG_COMMS_QUALITY,			coms_qual, signal_lvl, noise_lvl	 ) /*[STA] Quality of the Basic Service Set connection		*/
 X1( CFG_CUR_TX_RATE,			rate								 ) /*[STA] Actual transmit data rate						*/
 X1( CFG_CUR_BEACON_INTERVAL,	interval							 ) /*Beacon transmit interval time for BSS creation			*/
#if (HCF_TYPE) & HCF_TYPE_WARP
 X11( CFG_CUR_SCALE_THRH,											 \
	 carrier_detect_thrh_cck, carrier_detect_thrh_ofdm, defer_thrh,	 \
	 energy_detect_thrh, rssi_on_thrh_deviation, 					 \
	 rssi_off_thrh_deviation, cck_drop_thrh, ofdm_drop_thrh, 		 \
	 cell_search_thrh, out_of_range_thrh, delta_snr				 )
#else
 X6( CFG_CUR_SCALE_THRH,											 \
	 energy_detect_thrh, carrier_detect_thrh, defer_thrh, 			 \
	 cell_search_thrh, out_of_range_thrh, delta_snr					 ) /*Actual System Scale thresholds settings				*/
#endif // HCF_TYPE_WARP
 X1( CFG_PROTOCOL_RSP_TIME,		time								 ) /*Max time to await a response to a request message		*/
 X1( CFG_CUR_SHORT_RETRY_LIMIT,	limit								 ) /*Max number of transmit attempts for short frames		*/
 X1( CFG_CUR_LONG_RETRY_LIMIT,	limit								 ) /*Max number of transmit attempts for long frames		*/
 X1( CFG_MAX_TX_LIFETIME,		time								 ) /*Max transmit frame handling duration					*/
 X1( CFG_MAX_RX_LIFETIME,		time								 ) /*Max received frame handling duration					*/
 X1( CFG_CF_POLLABLE,			cf_pollable							 ) /*[STA] Contention Free pollable capability indication	*/
 X2( CFG_AUTHENTICATION_ALGORITHMS,authentication_type, type_enabled ) /*Authentication Algorithm								*/
 X1( CFG_PRIVACY_OPT_IMPLEMENTED,privacy_opt_implemented			 ) /*WEP Option availability indication						*/
 X1( CFG_CUR_REMOTE_RATES,		rates								 ) /*CurrentRemoteRates										*/
 X1( CFG_CUR_USED_RATES,		rates								 ) /*CurrentUsedRates										*/
 X1( CFG_CUR_SYSTEM_SCALE,		current_system_scale				 ) /*CurrentUsedRates										*/
 X1( CFG_CUR_TX_RATE1,			rate 								 ) /*[AP] Actual Port 1 transmit data rate					*/
 X1( CFG_CUR_TX_RATE2,			rate								 ) /*[AP] Actual Port 2 transmit data rate					*/
 X1( CFG_CUR_TX_RATE3,			rate								 ) /*[AP] Actual Port 3 transmit data rate					*/
 X1( CFG_CUR_TX_RATE4,			rate								 ) /*[AP] Actual Port 4 transmit data rate					*/
 X1( CFG_CUR_TX_RATE5,			rate								 ) /*[AP] Actual Port 5 transmit data rate					*/
 X1( CFG_CUR_TX_RATE6,			rate								 ) /*[AP] Actual Port 6 transmit data rate					*/
 X1( CFG_OWN_MAC_ADDR,			mac_addr[3]							 ) /*[AP] Unique local node MAC Address						*/
 X3( CFG_PCF_INFO,				medium_occupancy_limit, 			 \
		 						cfp_period, cfp_max_duration 		 ) /*[AP] Point Coordination Function capability info		*/
 X1( CFG_CUR_WPA_INFO_ELEMENT, ssn_info_element[1]				 	 ) /*    													*/
 X4( CFG_CUR_TKIP_IV_INFO, 											 \
		 tkip_seq_cnt0[4], tkip_seq_cnt1[4], 						 \
		 tkip_seq_cnt2[4], tkip_seq_cnt3[4]  						 ) /*    													*/
 X2( CFG_CUR_ASSOC_REQ_INFO,	frame_type, frame_body[1]			 ) /*	0xFD8C												*/
 X2( CFG_CUR_ASSOC_RESP_INFO,	frame_type, frame_body[1]			 ) /*	0xFD8D												*/


/*	Modem INFORMATION */
 X1( CFG_PHY_TYPE,				phy_type 							 ) /*Physical layer type indication							*/
 X1( CFG_CUR_CHANNEL,			current_channel						 ) /*Actual frequency channel used for transmission			*/
 X1( CFG_CUR_POWER_STATE,		current_power_state					 ) /*Actual power consumption status						*/
 X1( CFG_CCAMODE,				cca_mode							 ) /*Clear channel assessment mode indication				*/
 X1( CFG_SUPPORTED_DATA_RATES,	rates[5]							 ) /*Data rates capability information						*/


/* FRAMES */
XX1( CFG_SCAN,					SCAN_RS_STRCT, scan_result[32]		 ) /*Scan results											*/



//--------------------------------------------------------------------------------------
// UIL management function to be passed to WaveLAN/IEEE Drivers in DUI_STRCT field fun
//--------------------------------------------------------------------------------------

// HCF and UIL Common
#define MDD_ACT_SCAN			0x06					// Hermes Inquire Scan (F101) command
#define MDD_ACT_PRS_SCAN 		0x07					// Hermes Probe Response Scan (F102) command

// UIL Specific
#define UIL_FUN_CONNECT			0x00					// Perform connect command
#define UIL_FUN_DISCONNECT		0x01					// Perform disconnect command
#define UIL_FUN_ACTION			0x02					// Perform UIL Action command.
#define UIL_FUN_SEND_DIAG_MSG	0x03					// Send a diagnostic message.
#define UIL_FUN_GET_INFO		0x04					// Retrieve information from NIC.
#define UIL_FUN_PUT_INFO		0x05					// Put information on NIC.

/*	UIL_ACT_TALLIES				0x05		 			* this should not be exported to the USF
											 			* it is solely intended as a strategic choice for the MSF to either
											 			* - use HCF_ACT_TALLIES and direct IFB access
														* - use CFG_TALLIES
														*/
#define UIL_ACT_SCAN			MDD_ACT_SCAN
#define UIL_ACT_PRS_SCAN 		MDD_ACT_PRS_SCAN
#define UIL_ACT_BLOCK	 		0x0B
#define UIL_ACT_UNBLOCK	 		0x0C
#define UIL_ACT_RESET	 		0x80
#define UIL_ACT_REBIND	 		0x81
#define UIL_ACT_APPLY	 		0x82
#define UIL_ACT_DISCONNECT		0x83	//;?040108 possibly obsolete	//Special for WINCE

// HCF Specific
/* Note that UIL_ACT-codes must match HCF_ACT-codes across a run-time bound I/F
 * The initial matching is achieved by "#define HCF_ACT_xxx HCF_UIL_ACT_xxx" where appropriate
 * In other words, these codes should never, ever change to minimize migration problems between
 * combinations of old drivers and new utilities and vice versa
 */
#define HCF_DISCONNECT			0x01					//disconnect request for hcf_connect (invalid as IO Address)
#define HCF_ACT_TALLIES 		0x05					// ! UIL_ACT_TALLIES does not exist ! Hermes Inquire Tallies (F100) cmd
#if ( (HCF_TYPE) & HCF_TYPE_WARP ) == 0
#define HCF_ACT_SCAN			MDD_ACT_SCAN
#endif // HCF_TYPE_WARP
#define HCF_ACT_PRS_SCAN		MDD_ACT_PRS_SCAN
#if HCF_INT_ON
#define HCF_ACT_INT_OFF 		0x0D					// Disable Interrupt generation
#define HCF_ACT_INT_ON			0x0E					// Enable Interrupt generation
#define HCF_ACT_INT_FORCE_ON	0x0F					// Enforce Enable Interrupt generation
#endif // HCF_INT_ON
#define HCF_ACT_RX_ACK			0x15					// Receiever ACK (optimization)
#if (HCF_TYPE) & HCF_TYPE_CCX
#define HCF_ACT_CCX_ON			0x1A					// enable CKIP
#define HCF_ACT_CCX_OFF			0x1B					// disable CKIP
#endif // HCF_TYPE_CCX
#if (HCF_SLEEP) & HCF_DDS
#define HCF_ACT_SLEEP			0x1C					// DDS Sleep request
//#define HCF_ACT_WAKEUP		0x1D					// DDS Wakeup request
#endif // HCF_DDS

/*	HCF_ACT_MAX							// xxxx: start value for UIL-range, NOT to be passed to HCF
 *										Too bad, there was originally no spare room created to use
 *										HCF_ACT_MAX as an equivalent of HCF_ERR_MAX. Since creating
 *										this room in retrospect would create a backward incompatibility
 *										we will just have to live with the haphazard sequence of
 *										UIL- and HCF specific codes. Theoretically this could be
 *										corrected when and if there will ever be an overall
 *										incompatibility introduced for another reason
 */

/*============================================================= HERMES RECORDS	============================*/
#define CFG_RID_FW_MIN							0xFA00	//lowest value representing a Hermes-II based RID
// #define CFG_PDA_BEGIN						0xFA	//
// #define CFG_PDA_END							0xFA	//
// #define CFG_PDA_NIC_TOP_LVL_ASSEMBLY_NUMBER	0xFA	//
// #define CFG_PDA_PCB_TRACER_NUMBER			0xFA	//
// #define CFG_PDA_RMM_TRACER_NUMBER			0xFA	//
// #define CFG_PDA_RMM_COMP_ID					0xFA	//
// #define CFG_PDA_								0xFA	//

/*============================================================= CONFIGURATION RECORDS	=====================*/
/*============================================================= mask 0xFCxx				=====================*/
#define CFG_RID_CFG_MIN					0xFC00		//lowest value representing a Hermes configuration  RID

//	NETWORK PARAMETERS, STATIC CONFIGURATION ENTITIES
//FC05, FC0B, FC0C, FC0D: SEE W2DN149

#define CFG_CNF_PORT_TYPE				0xFC00		//[STA] Connection control characteristics
#define CFG_CNF_OWN_MAC_ADDR			0xFC01		//[STA] MAC Address of this node
//										0xFC02		see DYNAMIC CONFIGURATION ENTITIES
#define CFG_CNF_OWN_CHANNEL				0xFC03		//Communication channel for BSS creation
#define CFG_CNF_OWN_SSID				0xFC04		//IBSS creation (STA) or ESS (AP) Service Set Ident
#define CFG_CNF_OWN_ATIM_WINDOW			0xFC05		//[STA] ATIM Window time for IBSS creation
#define CFG_CNF_SYSTEM_SCALE			0xFC06		//System Scale that specifies the AP density
#define CFG_CNF_MAX_DATA_LEN			0xFC07		//Maximum length of MAC Frame Body data
#define CFG_CNF_PM_ENABLED				0xFC09		//[STA] Switch for ESS Power Management (PM)
#define CFG_CNF_MCAST_RX				0xFC0B		//[STA] Switch for ESS PM Multicast reception On/Off
#define CFG_CNF_MAX_SLEEP_DURATION		0xFC0C		//[STA] Maximum sleep time for ESS PM
#define CFG_CNF_HOLDOVER_DURATION		0xFC0D		//[STA] Holdover time for ESS PM
#define CFG_CNF_OWN_NAME				0xFC0E		//Identification text for diagnostic purposes

#define CFG_CNF_OWN_DTIM_PERIOD			0xFC10		//[AP] Beacon intervals between successive DTIMs
#define CFG_CNF_WDS_ADDR1				0xFC11		//[AP] Port 1 MAC Adrs of corresponding WDS Link node
#define CFG_CNF_WDS_ADDR2				0xFC12		//[AP] Port 2 MAC Adrs of corresponding WDS Link node
#define CFG_CNF_WDS_ADDR3				0xFC13		//[AP] Port 3 MAC Adrs of corresponding WDS Link node
#define CFG_CNF_WDS_ADDR4				0xFC14		//[AP] Port 4 MAC Adrs of corresponding WDS Link node
#define CFG_CNF_WDS_ADDR5				0xFC15		//[AP] Port 5 MAC Adrs of corresponding WDS Link node
#define CFG_CNF_WDS_ADDR6				0xFC16		//[AP] Port 6 MAC Adrs of corresponding WDS Link node
#define CFG_CNF_PM_MCAST_BUF			0xFC17		//[AP] Switch for PM buffereing of Multicast Messages
#define CFG_CNF_MCAST_PM_BUF			CFG_CNF_PM_MCAST_BUF	//name does not match H-II spec
#define CFG_CNF_REJECT_ANY				0xFC18		//[AP] Switch for PM buffereing of Multicast Messages

#define CFG_CNF_ENCRYPTION				0xFC20		//select en/de-cryption of Tx/Rx messages
#define CFG_CNF_AUTHENTICATION			0xFC21		//[STA] selects Authentication algorithm
#define CFG_CNF_EXCL_UNENCRYPTED		0xFC22		//[AP] Switch for 'clear-text' rx message acceptance
#define CFG_CNF_MCAST_RATE				0xFC23		//Transmit Data rate for Multicast frames
#define CFG_CNF_INTRA_BSS_RELAY			0xFC24		//[AP] Switch for IntraBBS relay
#define CFG_CNF_MICRO_WAVE				0xFC25		//MicroWave (Robustness)
#define CFG_CNF_LOAD_BALANCING		 	0xFC26		//Load Balancing		 (Boolean, 0=OFF, 1=ON, default=1)
#define CFG_CNF_MEDIUM_DISTRIBUTION	 	0xFC27		//Medium Distribution	 (Boolean, 0=OFF, 1=ON, default=1)
#define CFG_CNF_RX_ALL_GROUP_ADDR		0xFC28		//[STA] Group Address Filter
#define CFG_CNF_COUNTRY_INFO			0xFC29		//Country Info
#if (HCF_TYPE) & HCF_TYPE_WARP
#define CFG_CNF_TX_POW_LVL				0xFC2A		//TxPower Level
#define CFG_CNF_CONNECTION_CNTL			0xFC30		//[STA] Connection Control
#define CFG_CNF_OWN_BEACON_INTERVAL		0xFC31		//[AP]
#define CFG_CNF_SHORT_RETRY_LIMIT		0xFC32		//
#define CFG_CNF_LONG_RETRY_LIMIT		0xFC33		//
#define CFG_CNF_TX_EVENT_MODE			0xFC34		//
#define CFG_CNF_WIFI_COMPATIBLE			0xFC35		//[STA] Wifi compatible
#endif // HCF_TYPE_WARP
#if (HCF_TYPE) & HCF_TYPE_BEAGLE_HII5
#define CFG_VOICE_RETRY_LIMIT			0xFC36		/* Voice frame retry limit. Range: 1-15, default: 4 */
#define CFG_VOICE_CONTENTION_WINDOW		0xFC37		/* Contention window for voice frames. */
#endif	// BEAGLE_HII5

//	NETWORK PARAMETERS, DYNAMIC CONFIGURATION ENTITIES
#define CFG_DESIRED_SSID				0xFC02		//[STA] Service Set identification for connection and scan

#define CFG_GROUP_ADDR					0xFC80		//[STA] Multicast MAC Addresses for Rx-message
#define CFG_CREATE_IBSS					0xFC81		//[STA] Switch for IBSS creation On/Off
#define CFG_RTS_THRH					0xFC83		//Frame length used for RTS/CTS handshake
#define CFG_TX_RATE_CNTL				0xFC84		//[STA] Data rate control for message transmission
#define CFG_PROMISCUOUS_MODE			0xFC85		//[STA] Switch for Promiscuous mode reception On/Off
#define CFG_WOL							0xFC86		//[STA] Switch for Wake-On-LAN mode
#define CFG_WOL_PATTERNS				0xFC87		//[STA] Patterns for Wake-On-LAN
#define CFG_SUPPORTED_RATE_SET_CNTL		0xFC88		//
#define CFG_BASIC_RATE_SET_CNTL			0xFC89		//

#define CFG_SOFTWARE_ACK_MODE			0xFC90		//
#define CFG_RTS_THRH0					0xFC97		//[AP] Port 0 frame length for RTS/CTS handshake
#define CFG_RTS_THRH1					0xFC98		//[AP] Port 1 frame length for RTS/CTS handshake
#define CFG_RTS_THRH2					0xFC99		//[AP] Port 2 frame length for RTS/CTS handshake
#define CFG_RTS_THRH3					0xFC9A		//[AP] Port 3 frame length for RTS/CTS handshake
#define CFG_RTS_THRH4					0xFC9B		//[AP] Port 4 frame length for RTS/CTS handshake
#define CFG_RTS_THRH5					0xFC9C		//[AP] Port 5 frame length for RTS/CTS handshake
#define CFG_RTS_THRH6					0xFC9D		//[AP] Port 6 frame length for RTS/CTS handshake

#define CFG_TX_RATE_CNTL0				0xFC9E		//[AP] Port 0 data rate control for transmission
#define CFG_TX_RATE_CNTL1				0xFC9F		//[AP] Port 1 data rate control for transmission
#define CFG_TX_RATE_CNTL2				0xFCA0		//[AP] Port 2 data rate control for transmission
#define CFG_TX_RATE_CNTL3				0xFCA1		//[AP] Port 3 data rate control for transmission
#define CFG_TX_RATE_CNTL4				0xFCA2		//[AP] Port 4 data rate control for transmission
#define CFG_TX_RATE_CNTL5				0xFCA3		//[AP] Port 5 data rate control for transmission
#define CFG_TX_RATE_CNTL6				0xFCA4		//[AP] Port 6 data rate control for transmission

#define CFG_DEFAULT_KEYS				0xFCB0		//defines set of encryption keys
#define CFG_TX_KEY_ID					0xFCB1		//select key for encryption of Tx messages
#define CFG_SCAN_SSID					0xFCB2		//Scan SSID
#define CFG_ADD_TKIP_DEFAULT_KEY		0xFCB4		//set KeyID and TxKey indication
#define 	KEY_ID							0x0003		//KeyID mask for tkip_key_id_info field
#define 	TX_KEY							0x8000		//Default Tx Key flag of tkip_key_id_info field
#define CFG_SET_WPA_AUTH_KEY_MGMT_SUITE	0xFCB5		//Authenticated Key Management Suite
#define CFG_REMOVE_TKIP_DEFAULT_KEY		0xFCB6		//invalidate KeyID and TxKey indication
#define CFG_ADD_TKIP_MAPPED_KEY			0xFCB7		//set MAC address pairwise station
#define CFG_REMOVE_TKIP_MAPPED_KEY		0xFCB8		//invalidate MAC address pairwise station
#define CFG_SET_WPA_CAPABILITIES_INFO	0xFCB9		//WPA Capabilities
#define CFG_CACHED_PMK_ADDR				0xFCBA		//set MAC address of pre-authenticated AP
#define CFG_REMOVE_CACHED_PMK_ADDR		0xFCBB		//invalidate MAC address of pre-authenticated AP
#define CFG_FCBC	0xFCBC	//FW codes ahead of available documentation, so ???????
#define CFG_FCBD	0xFCBD	//FW codes ahead of available documentation, so ???????
#define CFG_FCBE	0xFCBE	//FW codes ahead of available documentation, so ???????
#define CFG_FCBF	0xFCBF	//FW codes ahead of available documentation, so ???????

#define CFG_HANDOVER_ADDR				0xFCC0		//[AP] Station MAC Address re-associated with other AP
#define CFG_SCAN_CHANNEL				0xFCC2		//Channel set for host requested scan
//;?#define CFG_SCAN_CHANNEL_MASK			0xFCC2		// contains
#define CFG_DISASSOCIATE_ADDR			0xFCC4		//[AP] Station MAC Address to be disassociated
#define CFG_PROBE_DATA_RATE				0xFCC5		//WARP connection control
#define CFG_FRAME_BURST_LIMIT			0xFCC6		//
#define CFG_COEXISTENSE_BEHAVIOUR		0xFCC7		//[AP]
#define CFG_DEAUTHENTICATE_ADDR			0xFCC8		//MAC address of Station to be deauthenticated

//	BEHAVIOR PARAMETERS
#define CFG_TICK_TIME					0xFCE0		//Auxiliary Timer tick interval
#define CFG_DDS_TICK_TIME				0xFCE1		//Disconnected DeepSleep Timer tick interval
//#define CFG_CNF_COUNTRY					0xFCFE	apparently not needed ;?
#define CFG_RID_CFG_MAX					0xFCFF		//highest value representing an Configuration RID


/*============================================================= INFORMATION RECORDS 	=====================*/
/*============================================================= mask 0xFDxx				=====================*/
//	NIC INFORMATION
#define CFG_RID_INF_MIN					0xFD00	//lowest value representing an Information RID
#define CFG_MAX_LOAD_TIME				0xFD00	//[INT] Maximum response time of the Download command.
#define CFG_DL_BUF						0xFD01	//[INT] Download buffer location and size.
#define CFG_PRI_IDENTITY				0xFD02	//[PRI] Primary Functions firmware identification.
#define CFG_PRI_SUP_RANGE				0xFD03	//[PRI] Primary Functions I/F Supplier compatibility range.
#define CFG_NIC_HSI_SUP_RANGE			0xFD09	//H/W - S/W I/F supplier range
#define CFG_NIC_SERIAL_NUMBER			0xFD0A	//[PRI] Network Interface Card serial number.
#define CFG_NIC_IDENTITY				0xFD0B	//[PRI] Network Interface Card identification.
#define CFG_NIC_MFI_SUP_RANGE			0xFD0C	//[PRI] Modem I/F Supplier compatibility range.
#define CFG_NIC_CFI_SUP_RANGE			0xFD0D	//[PRI] Controller I/F Supplier compatibility range.
#define CFG_CHANNEL_LIST				0xFD10	//Allowed communication channels.
#define CFG_NIC_TEMP_TYPE  				0xFD12	//Hardware temperature range code.
#define CFG_CIS							0xFD13	//PC Card Standard Card Information Structure
#define CFG_NIC_PROFILE					0xFD14	//Card Profile
#define CFG_FW_IDENTITY					0xFD20	//firmware identification.
#define CFG_FW_SUP_RANGE				0xFD21	//firmware Supplier compatibility range.
#define CFG_MFI_ACT_RANGES_STA			0xFD22	//[STA] Modem I/F Actor compatibility ranges.
#define CFG_CFI_ACT_RANGES_STA			0xFD23	//[STA] Controller I/F Actor compatibility ranges.
#define CFG_NIC_BUS_TYPE				0xFD24	//Card Bustype
#define 	CFG_NIC_BUS_TYPE_PCCARD_CF		0x0000	//16 bit PC Card or Compact Flash
#define 	CFG_NIC_BUS_TYPE_USB			0x0001	//USB
#define 	CFG_NIC_BUS_TYPE_CARDBUS		0x0002	//CardBus
#define 	CFG_NIC_BUS_TYPE_PCI			0x0003	//(mini)PCI
#define CFG_DOMAIN_CODE						0xFD25

//	MAC INFORMATION
#define CFG_PORT_STAT					0xFD40	//Actual MAC Port connection control status
#define CFG_CUR_SSID					0xFD41	//[STA] Identification of the actually connected SS
#define CFG_CUR_BSSID					0xFD42	//[STA] Identification of the actually connected BSS
#define CFG_COMMS_QUALITY				0xFD43	//[STA] Quality of the Basic Service Set connection
#define CFG_CUR_TX_RATE					0xFD44	//[STA] Actual transmit data rate
#define CFG_CUR_BEACON_INTERVAL			0xFD45	//Beacon transmit interval time for BSS creation
#define CFG_CUR_SCALE_THRH				0xFD46	//Actual System Scale thresholds settings
#define CFG_PROTOCOL_RSP_TIME			0xFD47	//Max time to await a response to a request message
#define CFG_CUR_SHORT_RETRY_LIMIT		0xFD48	//Max number of transmit attempts for short frames
#define CFG_CUR_LONG_RETRY_LIMIT		0xFD49	//Max number of transmit attempts for long frames
#define CFG_MAX_TX_LIFETIME				0xFD4A	//Max transmit frame handling duration
#define CFG_MAX_RX_LIFETIME				0xFD4B	//Max received frame handling duration
#define CFG_CF_POLLABLE					0xFD4C	//[STA] Contention Free pollable capability indication
#define CFG_AUTHENTICATION_ALGORITHMS	0xFD4D	//Available Authentication Algorithms indication
#define CFG_PRIVACY_OPT_IMPLEMENTED		0xFD4F	//WEP Option availability indication

#define CFG_CUR_REMOTE_RATES			0xFD50	//[STA] CurrentRemoteRates
#define CFG_CUR_USED_RATES				0xFD51	//[STA] CurrentUsedRates
#define CFG_CUR_SYSTEM_SCALE			0xFD52	//[STA] CurrentSystemScale

#define CFG_CUR_TX_RATE1				0xFD80	//[AP] Actual Port 1 transmit data rate
#define CFG_CUR_TX_RATE2				0xFD81	//[AP] Actual Port 2 transmit data rate
#define CFG_CUR_TX_RATE3				0xFD82	//[AP] Actual Port 3 transmit data rate
#define CFG_CUR_TX_RATE4				0xFD83	//[AP] Actual Port 4 transmit data rate
#define CFG_CUR_TX_RATE5				0xFD84	//[AP] Actual Port 5 transmit data rate
#define CFG_CUR_TX_RATE6				0xFD85	//[AP] Actual Port 6 transmit data rate
#define CFG_NIC_MAC_ADDR				0xFD86	//Unique local node MAC Address
#define CFG_PCF_INFO					0xFD87	//[AP] Point Coordination Function capability info
//*RESERVED* #define CFG_HIGHEST_BASIC_RATE			0xFD88	//
#define CFG_CUR_COUNTRY_INFO			0xFD89	//
#define CFG_CUR_WPA_INFO_ELEMENT		0xFD8A	//
#define CFG_CUR_TKIP_IV_INFO			0xFD8B	//
#define CFG_CUR_ASSOC_REQ_INFO			0xFD8C	//
#define CFG_CUR_ASSOC_RESP_INFO			0xFD8D	//
#define CFG_CUR_LOAD					0xFD8E	//[AP] current load on AP's channel

#define CFG_SECURITY_CAPABILITIES		0xFD90	//Combined capabilities information

//	MODEM INFORMATION
#define CFG_PHY_TYPE					0xFDC0	//Physical layer type indication
#define CFG_CUR_CHANNEL					0xFDC1	//Actual frequency channel used for transmission
#define CFG_CUR_POWER_STATE				0xFDC2	//Actual power consumption status
#define CFG_CCA_MODE					0xFDC3	//Clear channel assessment mode indication
#define CFG_SUPPORTED_DATA_RATES		0xFDC6	//Data rates capability information

#define CFG_RID_INF_MAX					0xFDFF	//highest value representing an Information RID

//	ENGINEERING INFORMATION
#define CFG_RID_ENG_MIN					0xFFE0	//lowest value representing a Hermes engineering RID


/****************************** General define *************************************************************/


//IFB field related
//		IFB_CardStat
#define CARD_STAT_INCOMP_PRI			0x2000U	// no compatible HSI / primary F/W
#define CARD_STAT_INCOMP_FW				0x1000U	// no compatible station / tertiary F/W
#define CARD_STAT_DEFUNCT				0x0100U	// HCF is in Defunct mode
//		IFB_RxStat
#define RX_STAT_PRIO					0x00E0U	//Priority subfield
#define RX_STAT_ERR						0x000FU	//Error mask
#define 	RX_STAT_UNDECR				0x0002U	//Non-decryptable encrypted message
#define 	RX_STAT_FCS_ERR				0x0001U	//FCS error

// SNAP header for E-II Encapsulation
#define ENC_NONE			            0xFF
#define ENC_1042    			        0x00
#define ENC_TUNNEL      	    		0xF8
/****************************** Xxxxxxxx *******************************************************************/


#define HCF_SUCCESS					0x00	// OK
#define HCF_ERR_TIME_OUT			0x04	// Expected Hermes event did not occur in expected time
#define HCF_ERR_NO_NIC				0x05	/* card not found (usually yanked away during hcfio_in_string
										  	 * Also: card is either absent or disabled while it should be neither */
#define HCF_ERR_LEN					0x08	/* buffer size insufficient
		 								  	 *		  -	IFB_ConfigTable too small
		 								  	 *		  -	hcf_get_info buffer has a size of 0 or 1 or less than needed
		 							  		 *			to accommodate all data
		 							  		 *		  -	hcf_put_info: CFG_DLNV_DATA exceeds intermediate
											 *		  buffer size */
#define HCF_ERR_INCOMP_PRI			0x09	// primary functions are not compatible
#define HCF_ERR_INCOMP_FW			0x0A	// station functions are compatible
#define HCF_ERR_MIC					0x0D	// MIC check fails
#define HCF_ERR_SLEEP				0x0E	// NIC in sleep mode
#define HCF_ERR_MAX					0x3F	/* end of HCF range
											   *** ** *** ****** *** *************** */
#define HCF_ERR_DEFUNCT				0x80	// BIT, reflecting that the HCF is in defunct mode (bits 0x7F reflect cause)
#define HCF_ERR_DEFUNCT_AUX			0x82	// Timeout on acknowledgement on en/disabling AUX registers
#define HCF_ERR_DEFUNCT_TIMER		0x83	// Timeout on timer calibration during initialization process
#define HCF_ERR_DEFUNCT_TIME_OUT	0x84	// Timeout on Busy bit drop during BAP setup
#define HCF_ERR_DEFUNCT_CMD_SEQ		0x86	// Hermes and HCF are out of sync in issuing/processing commands

#define HCF_INT_PENDING				0x01	// return status of hcf_act( HCF_ACT_INT_OFF )

#define HCF_PORT_0 					0x0000	// Station supports only single MAC Port
#define HCF_PORT_1 					0x0100	// HCF_PORT_1 through HCF_PORT_6 are only supported by AP F/W
#define HCF_PORT_2 					0x0200
#define HCF_PORT_3 					0x0300
#define HCF_PORT_4 					0x0400
#define HCF_PORT_5 					0x0500
#define HCF_PORT_6 					0x0600

#define HCF_CNTL_ENABLE				0x01
#define HCF_CNTL_DISABLE			0x02
#define HCF_CNTL_CONNECT			0x03
#define HCF_CNTL_DISCONNECT			0x05
#define HCF_CNTL_CONTINUE			0x07

#define USE_DMA 					0x0001
#define USE_16BIT 					0x0002
#define DMA_ENABLED					0x8000	//weak name, it really means: F/W enabled and DMA selected

//#define HCF_DMA_FD_CNT	 		(2*29) 						//size in bytes of one Tx/RxFS minus DA/SA
//;?the MSF ( H2PCI.C uses the next 2 mnemonics )
#define HCF_DMA_RX_BUF1_SIZE		(HFS_ADDR_DEST + 8)			//extra bytes for LEN/SNAP if decapsulation
#define HCF_DMA_TX_BUF1_SIZE		(HFS_ADDR_DEST + 2*6 + 8)	//extra bytes for DA/SA/LEN/SNAP if encapsulation

//HFS_TX_CNTL
/* Note that the HCF_.... System Constants influence the HFS_.... values below
 *                              H-I     H-I  |  H-II    H-II    H-II.5
 *                                      WPA  |          WPA
 * HFS_TX_CNTL_TX_OK            0002    0002 |  0002    0002     N/A    <<<<<<<<deprecated
 * HFS_TX_CNTL_TX_EX            0004    0004 |  0004    0004     N/A
 * HFS_TX_CNTL_MIC               N/A    0010 |   N/A    0010     N/A
 * HFS_TX_CNTL_TID               N/A     N/A |   N/A     N/A    000F
 * HFS_TX_CNTL_SERVICE_CLASS     N/A     N/A |   N/A     N/A    00C0
 * HFS_TX_CNTL_PORT             0700    0700 |  0700    0700    0700
 * HFS_TX_CNTL_MIC_KEY_ID       1800    1800 |  0000    1800     N/A
 * HFS_TX_CNTL_CKIP             0000    0000 |  0000    2000    2000
 * HFS_TX_CNTL_TX_DELAY         4000    4000 |  4000    4000     N/A
 * HFS_TX_CNTL_ACTION            N/A     N/A |   N/A     N/A    4000
 *                              ====    ==== |  ====    ====    ====
 *                              5F06    5F16 |  4706    7F06    67CF
 *
 * HCF_TX_CNTL_MASK specifies the bits allowed on the Host I/F
 * note: bit 0x4000 has different meaning for H-II and H-II.5
 * note: [] indicate bits which are possibly added by the HCF to TxControl at the Host I/F
 * note: () indicate bits which are supposedly never ever used in a WCI environment
 * note: ? denote bits which seem not to be documented in the documents I have available
 */
//H-I:     HCF_TX_CNTL_MASK	0x47FE	//TX_DELAY, MACPort, Priority, (StrucType), TxEx, TxOK
//H-I WPA: HCF_TX_CNTL_MASK	0x5FE6	//TX_DELAY, MICKey, MACPort, Priority, (StrucType), TxEx, TxOK
#if (HCF_TYPE) & HCF_TYPE_WARP
#define  HCF_TX_CNTL_MASK	0x27E7	//no TX_DELAY?, CCX, MACPort, Priority, (StrucType), TxEx, TxOK, Spectralink
//#elif (HCF_TYPE) & HCF_TYPE_WPA
//#define  HCF_TX_CNTL_MASK	0x7F06	//TX_DELAY, CKIP?, MICKeyID, MACPort, [MIC],TxEx, TxOK (TAR419D7)
#else
#define  HCF_TX_CNTL_MASK	0x67E7	//TX_DELAY?, CCX, MACPort, Priority, (StrucType), TxEx, TxOK, Spectralink
#endif // HCF_TYPE_WARP

#define HFS_TX_CNTL_TX_EX			0x0004U

#if (HCF_TYPE) & HCF_TYPE_WPA
#define HFS_TX_CNTL_MIC				0x0010U	//802.3 format with TKIP		;?changes to 0x0008 for H-II
#define HFS_TX_CNTL_MIC_KEY_ID		0x1800U	//MIC Key ID subfield
#endif // HCF_TYPE_WPA

#define HFS_TX_CNTL_PORT			0x0700U	//Port subfield of TxControl field of Transmit Frame Structure

#if (HCF_TYPE) & HCF_TYPE_CCX
#define HFS_TX_CNTL_CKIP			0x2000U	//CKIP encrypted flag
#endif // HCF_TYPE_CCX

#if (HCF_TYPE) & HCF_TYPE_TX_DELAY
#define HFS_TX_CNTL_TX_DELAY		0x4000U	//decouple "put data" and send
#endif // HCF_TYPE_TX_DELAY
#define HFS_TX_CNTL_TX_CONT			0x4000u	//engineering: continuous transmit

/*============================================================= HCF Defined RECORDS	=========================*/
#define CFG_PROD_DATA					0x0800 		//Plug Data (Engineering Test purposes only)
#define CFG_DL_EEPROM					0x0806		//Up/Download I2PROM for USB
#define		CFG_PDA							0x0002		//Download PDA
#define		CFG_MEM_I2PROM					0x0004		//Up/Download EEPROM

#define		CFG_MEM_READ					0x0000
#define		CFG_MEM_WRITE					0x0001

#define CFG_NULL						0x0820		//Empty Mail Box Info Block
#define CFG_MB_INFO						0x0820		//Mail Box Info Block
#define CFG_WMP							0x0822		//WaveLAN Management Protocol

#if defined MSF_COMPONENT_ID
#define CFG_DRV_INFO					0x0825		//Driver Information structure (see CFG_DRV_INFO_STRCT for details)
#define CFG_DRV_IDENTITY				0x0826		//driver identity (see CFG_DRV_IDENTITY_STRCT for details)
#define CFG_DRV_SUP_RANGE				0x0827      //Supplier range of driver - utility I/F
#define CFG_DRV_ACT_RANGES_PRI			0x0828      //(Acceptable) Actor range for Primary Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_STA			0x0829      //(Acceptable) Actor range for Station Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_HSI 			0x082A      //(Acceptable) Actor range for H/W - driver I/F
#define CFG_DRV_ACT_RANGES_APF			0x082B		//(Acceptable) Actor range for AP Firmware - driver I/F
#define CFG_HCF_OPT						0x082C		//HCF (Compile time) options
#endif // MSF_COMPONENT_ID

#define CFG_REG_MB						0x0830		//Register Mail Box
#define CFG_MB_ASSERT					0x0831		//Assert information
#define CFG_REG_ASSERT_RTNP				0x0832		//(de-)register MSF Assert Callback routine
#if (HCF_EXT) & HCF_EXT_INFO_LOG
#define CFG_REG_INFO_LOG				0x0839		//(de-)register Info frames to Log
#endif // HCF_INFO_LOG
#define CFG_CNTL_OPT					0x083A		//Control options

#define CFG_PROG						0x0857		//Program NIC memory
#define 	CFG_PROG_STOP					0x0000
#define 	CFG_PROG_VOLATILE				0x0100
//#define 	CFG_PROG_FLASH					0x0300		//restore if H-II non-volatile is introduced
//#define 	CFG_PROG_SEEPROM				0x1300		//restore if H-II non-volatile is introduced
#define 	CFG_PROG_SEEPROM_READBACK 		0x0400

#define CFG_FW_PRINTF                       0x0858      //Related to firmware debug printf functionality
#define CFG_FW_PRINTF_BUFFER_LOCATION       0x0859      //Also related to firmware debug printf functionality

#define CFG_CMD_NIC						0x0860		//Hermes Engineering command
#define CFG_CMD_HCF						0x0863		//HCF Engineering command
#define 	CFG_CMD_HCF_REG_ACCESS			0x0000	//Direct register access
#define 	CFG_CMD_HCF_RX_MON				0x0001	//Rx-monitor


/*============================================================= MSF Defined RECORDS	========================*/
#define CFG_ENCRYPT_STRING				0x0900		//transfer encryption info from CPL to MSF
#define CFG_AP_MODE						0x0901		//control mode of STAP driver from CPL
#define CFG_DRIVER_ENABLE				0x0902		//extend&export En-/Disable facility to Utility
#define CFG_PCI_COMMAND					0x0903		//PCI adapter (Ooievaar) structure
#define CFG_WOLAS_ENABLE				0x0904		//extend&export En-/Disable WOLAS facility to Utility
#define CFG_COUNTRY_STRING				0x0905		//transfer CountryInfo info from CPL to MSF
#define CFG_FW_DUMP						0x0906		//transfer nic memory to utility
#define CFG_POWER_MODE					0x0907		//controls the PM mode of the card
#define CFG_CONNECTION_MODE				0x0908		//controls the mode of the FW (ESS/AP/IBSS/ADHOC)
#define CFG_IFB							0x0909		//byte wise copy of IFB
#define CFG_MSF_TALLIES					0x090A		//MSF tallies (int's, rx and tx)
#define CFG_CURRENT_LINK_STATUS			0x090B		//Latest link status got through 0xF200 LinkEvent

/*============================================================ INFORMATION FRAMES =========================*/
#define CFG_INFO_FRAME_MIN				0xF000		//lowest value representing an Informatio Frame

#define CFG_TALLIES						0xF100		//Communications Tallies
#define CFG_SCAN						0xF101		//Scan results
#define CFG_PRS_SCAN					0xF102		//Probe Response Scan results

#define CFG_LINK_STAT 					0xF200		//Link Status
	/* 1 through 5 are F/W defined values, produced by CFG_LINK_STAT frame
	 * 1 through 5 are shared by CFG_LINK_STAT, IFB_LinkStat and IFB_DSLinkStat
	 * 1 plays a double role as CFG_LINK_STAT_CONNECTED and as bit reflecting:
	 *	 - connected: ON
	 *	 - disconnected: OFF
	 */
#define 	CFG_LINK_STAT_CONNECTED			0x0001
#define 	CFG_LINK_STAT_DISCONNECTED		0x0002
#define 	CFG_LINK_STAT_AP_CHANGE			0x0003
#define 	CFG_LINK_STAT_AP_OOR			0x0004
#define 	CFG_LINK_STAT_AP_IR				0x0005
#define 	CFG_LINK_STAT_FW				0x000F	//mask to isolate F/W defined bits
//#define 	CFG_LINK_STAT_TIMER				0x0FF0	//mask to isolate OOR timer
//#define 	CFG_LINK_STAT_DS_OOR			0x2000	//2000 and up are IFB_LinkStat specific
//#define 	CFG_LINK_STAT_DS_IR				0x4000
#define 	CFG_LINK_STAT_CHANGE			0x8000
#define CFG_ASSOC_STAT					0xF201		//Association Status
#define CFG_SECURITY_STAT				0xF202		//Security Status
#define CFG_UPDATED_INFO_RECORD			0xF204		//Updated Info Record

/*============================================================ CONFIGURATION RECORDS ======================*/
/***********************************************************************************************************/

/****************************** S T R U C T U R E   D E F I N I T I O N S **********************************/

//Quick&Dirty to get download for DOS ODI Hermes-II running typedef LTV_STRCT FAR *	LTVP;
typedef LTV_STRCT FAR *	LTVP;   // i.s.o #define LTVP LTV_STRCT FAR *

#if defined WVLAN_42 || defined WVLAN_43 //;?keepup with legacy a little while longer (4aug2003)
typedef struct DUI_STRCT {			/* "legacy", still used by WVLAN42/43, NDIS drivers use WLAPI			*/
	void  FAR	*ifbp;				/* Pointer to IFB
									 *	returned from MSF to USF by uil_connect
				 					 *	passed from USF to MSF as a "magic cookie" by all other UIL function calls
				 					 */
	hcf_16		stat;				// status returned from MSF to USF
	hcf_16		fun;				// command code from USF to MSF
	LTV_STRCT	ltv;				/* LTV structure
			 						 *** during uil_put_info:
						 			 *	  the L, T and V-fields carry information from USF to MSF
									 *** during uil_get_info:
									 *	  the L and T fields carry information from USF to MSF
									 *	  the L and V-fields carry information from MSF to USF
			 						 */
} DUI_STRCT;
typedef DUI_STRCT FAR *	DUIP;
#endif //defined WVLAN_42 || defined WVLAN_43 //;?keepup with legacy a liitle while longer (4aug2003)


typedef struct CFG_CMD_NIC_STRCT {	// CFG_CMD_NIC (0x0860)		Hermes Engineering command
	hcf_16	len;					//default length of RID
	hcf_16	typ;					//RID identification as defined by Hermes
	hcf_16	cmd;					//Command code (0x003F) and control bits (0xFFC0)
	hcf_16	parm0;					//parameters for Hermes Param0 register
	hcf_16	parm1;					//parameters for Hermes Param1 register
	hcf_16	parm2;					//parameters for Hermes Param2 register
	hcf_16	stat;					//result code from Hermes Status register
	hcf_16	resp0;					//responses from Hermes Resp0 register
	hcf_16	resp1;					//responses from Hermes Resp1 register
	hcf_16	resp2;					//responses from Hermes Resp2 register
	hcf_16	hcf_stat;				//result code from cmd_exe routine
	hcf_16	ifb_err_cmd;			//IFB_ErrCmd
	hcf_16	ifb_err_qualifier;		//IFB_ErrQualifier
} CFG_CMD_NIC_STRCT;


typedef struct CFG_DRV_INFO_STRCT {		//CFG_DRV_INFO (0x0825) driver information
	hcf_16	len;					//default length of RID
	hcf_16	typ;					//RID identification as defined by Hermes
	hcf_8	driver_name[8];			//Driver name, 8 bytes, right zero padded
	hcf_16	driver_version;			//BCD 2 digit major and 2 digit minor driver version
	hcf_16	HCF_version;   			//BCD 2 digit major and 2 digit minor HCF version
	hcf_16	driver_stat;			//
	hcf_16	IO_address;				//base IO address used by NIC
	hcf_16	IO_range;				//range of IO addresses used by NIC
	hcf_16	IRQ_number;				//Interrupt used by NIC
	hcf_16	card_stat;				/*NIC status
									@*	0x8000	Card present
									@*	0x4000	Card Enabled
									@*	0x2000	Driver incompatible with NIC Primary Functions
									@*	0x1000	Driver incompatible with NIC Station Functions				*/
	hcf_16	frame_type;				/*Frame type
									@*	0x000	802.3
									@*	0x008	802.11														*/
	hcf_32	drv_info;				/*driver specific info
									 * CE: virtual I/O base													*/
}CFG_DRV_INFO_STRCT;

#define COMP_ID_FW_PRI					21		//Primary Functions Firmware
#define COMP_ID_FW_INTERMEDIATE			22		//Intermediate Functions Firmware
#define COMP_ID_FW_STA					31		//Station Functions Firmware
#define COMP_ID_FW_AP					32		//AP Functions Firmware
#define COMP_ID_FW_AP_FAKE			   331		//AP Functions Firmware

#define COMP_ID_MINIPORT_NDIS_31		41		//Windows 9x/NT Miniport NDIS 3.1
#define COMP_ID_PACKET					42		//Packet
#define COMP_ID_ODI_16					43		//DOS ODI
#define COMP_ID_ODI_32					44		//32-bits ODI
#define COMP_ID_MAC_OS					45		//Macintosh OS
#define COMP_ID_WIN_CE					46		//Windows CE Miniport
//#define COMP_ID_LINUX_PD				47		//Linux, HCF-light based, MSF source code in Public Domain
#define COMP_ID_MINIPORT_NDIS_50		48		//Windows 9x/NT Miniport NDIS 5.0
#define COMP_ID_LINUX					49		/*Linux, GPL'ed HCF based, full source code in Public Domain
										  		 *thanks to Andreas Neuhaus								*/
#define COMP_ID_QNX						50		//QNX
#define COMP_ID_MINIPORT_NDIS_50_USB	51		//Windows 9x/NT Miniport NDIS 4.0
#define COMP_ID_MINIPORT_NDIS_40		52		//Windows 9x/NT Miniport NDIS 4.0
#define COMP_ID_VX_WORKS_ENDSTA			53		// VxWorks END Station driver
#define COMP_ID_VX_WORKS_ENDAP			54		// VxWorks END Access Point driver
//;?#define COMP_ID_MAC_OS_????			55		//;?check with HM
#define COMP_ID_VX_WORKS_END			56		// VxWorks END Station/Access Point driver
//										57		//NucleusOS@ARM Driver.
#define COMP_ID_WSU						63		/* WaveLAN Station Firmware Update utility
												 *	variant 1: Windows
												 *	variant 2: DOS
												 */
#define COMP_ID_AP1						81		//WaveLAN/IEEE AP
#define COMP_ID_EC						83		//WaveLAN/IEEE Ethernet Converter
#define COMP_ID_UBL						87		//USB Boot Loader

#define COMP_ROLE_SUPL					0x00	//supplier
#define COMP_ROLE_ACT					0x01	//actor

												//Supplier			  - actor
#define COMP_ID_MFI						0x01	//Modem		 		  - Firmware	I/F
#define COMP_ID_CFI						0x02	//Controller		  - Firmware	I/F
#define COMP_ID_PRI						0x03	//Primary Firmware	  - Driver		I/F
#define COMP_ID_STA						0x04	//Station Firmware	  - Driver		I/F
#define COMP_ID_DUI						0x05	//Driver			  - Utility		I/F
#define COMP_ID_HSI						0x06	//H/W                 - Driver		I/F
#define COMP_ID_DAI						0x07	//API                 - Driver		I/F
#define COMP_ID_APF						0x08	//H/W                 - Driver		I/F
#define COMP_ID_INT						0x09	//Intermediate FW     - Driver		I/F

#ifdef HCF_LEGACY
#define HCF_ACT_ACS_SCAN				HCF_ACT_PRS_SCAN
#define UIL_ACT_ACS_SCAN 				UIL_ACT_PRS_SCAN
#define MDD_ACT_ACS_SCAN	 			MDD_ACT_PRS_SCAN
#define CFG_ACS_SCAN					CFG_PRS_SCAN
#endif // HCF_LEGACY

#endif // MDD_H

