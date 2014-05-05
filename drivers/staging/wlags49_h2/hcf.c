/************************************************************************************************************
 *
 * FILE   :  HCF.C
 *
 * DATE    : $Date: 2004/08/05 11:47:10 $   $Revision: 1.10 $
 * Original: 2004/06/02 10:22:22    Revision: 1.85      Tag: hcf7_t20040602_01
 * Original: 2004/04/15 09:24:41    Revision: 1.63      Tag: hcf7_t7_20040415_01
 * Original: 2004/04/13 14:22:44    Revision: 1.62      Tag: t7_20040413_01
 * Original: 2004/04/01 15:32:55    Revision: 1.59      Tag: t7_20040401_01
 * Original: 2004/03/10 15:39:27    Revision: 1.55      Tag: t20040310_01
 * Original: 2004/03/04 11:03:37    Revision: 1.53      Tag: t20040304_01
 * Original: 2004/03/02 14:51:21    Revision: 1.50      Tag: t20040302_03
 * Original: 2004/02/24 13:00:27    Revision: 1.43      Tag: t20040224_01
 * Original: 2004/02/19 10:57:25    Revision: 1.39      Tag: t20040219_01
 *
 * AUTHOR :  Nico Valster
 *
 * SPECIFICATION: ........
 *
 * DESCRIPTION : HCF Routines for Hermes-II (callable via the Wireless Connection I/F or WCI)
 *               Local Support Routines for above procedures
 *
 *           Customizable via HCFCFG.H, which is included by HCF.H
 *
 *************************************************************************************************************
 *
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * COPYRIGHT © 1994 - 1995   by AT&T.                All Rights Reserved
 * COPYRIGHT © 1996 - 2000 by Lucent Technologies.   All Rights Reserved
 * COPYRIGHT © 2001 - 2004   by Agere Systems Inc.   All Rights Reserved
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
 **
 ** Implementation Notes
 **
 * - a leading marker of //! is used. The purpose of such a sequence is to help to understand the flow
 *   An example is:  //!rc = HCF_SUCCESS;
 *   if this is superfluous because rc is already guaranteed to be 0 but it shows to the (maintenance)
 *   programmer it is an intentional omission at the place where someone could consider it most appropriate at
 *   first glance
 * - using near pointers in a model where ss!=ds is an invitation for disaster, so be aware of how you specify
 *   your model and how you define variables which are used at interrupt time
 * - remember that sign extension on 32 bit platforms may cause problems unless code is carefully constructed,
 *   e.g. use "(hcf_16)~foo" rather than "~foo"
 *
 ************************************************************************************************************/

#include "hcf.h"                // HCF and MSF common include file
#include "hcfdef.h"             // HCF specific include file
#include "mmd.h"                // MoreModularDriver common include file
#include <linux/bug.h>
#include <linux/kernel.h>

#if ! defined offsetof
#define offsetof(s,m)   ((unsigned int)&(((s *)0)->m))
#endif // offsetof


/***********************************************************************************************************/
/***************************************  PROTOTYPES  ******************************************************/
/***********************************************************************************************************/
HCF_STATIC int          cmd_exe( IFBP ifbp, hcf_16 cmd_code, hcf_16 par_0 );
HCF_STATIC int          init( IFBP ifbp );
HCF_STATIC int          put_info( IFBP ifbp, LTVP ltvp );
HCF_STATIC int          put_info_mb( IFBP ifbp, CFG_MB_INFO_STRCT FAR * ltvp );
#if (HCF_TYPE) & HCF_TYPE_WPA
HCF_STATIC void         calc_mic( hcf_32* p, hcf_32 M );
void                    calc_mic_rx_frag( IFBP ifbp, wci_bufp p, int len );
void                    calc_mic_tx_frag( IFBP ifbp, wci_bufp p, int len );
HCF_STATIC int          check_mic( IFBP ifbp );
#endif // HCF_TYPE_WPA

HCF_STATIC void         calibrate( IFBP ifbp );
HCF_STATIC int          cmd_cmpl( IFBP ifbp );
HCF_STATIC hcf_16       get_fid( IFBP ifbp );
HCF_STATIC void         isr_info( IFBP ifbp );
#if HCF_DMA
HCF_STATIC DESC_STRCT*  get_frame_lst(IFBP ifbp, int tx_rx_flag);
#endif // HCF_DMA
HCF_STATIC void         get_frag( IFBP ifbp, wci_bufp bufp, int len BE_PAR( int word_len ) );   //char*, byte count (usually even)
#if HCF_DMA
HCF_STATIC void         put_frame_lst( IFBP ifbp, DESC_STRCT *descp, int tx_rx_flag );
#endif // HCF_DMA
HCF_STATIC void         put_frag( IFBP ifbp, wci_bufp bufp, int len BE_PAR( int word_len ) );
HCF_STATIC void         put_frag_finalize( IFBP ifbp );
HCF_STATIC int          setup_bap( IFBP ifbp, hcf_16 fid, int offset, int type );
#if (HCF_ASSERT) & HCF_ASSERT_PRINTF
static int fw_printf(IFBP ifbp, CFG_FW_PRINTF_STRCT FAR *ltvp);
#endif // HCF_ASSERT_PRINTF

HCF_STATIC int          download( IFBP ifbp, CFG_PROG_STRCT FAR *ltvp );
HCF_STATIC hcf_8        hcf_encap( wci_bufp type );
HCF_STATIC hcf_8        null_addr[4] = { 0, 0, 0, 0 };
#if ! defined IN_PORT_WORD          //replace I/O Macros with logging facility
extern FILE *log_file;

#define IN_PORT_WORD(port)          in_port_word( (hcf_io)(port) )

static hcf_16 in_port_word( hcf_io port ) {
	hcf_16 i = (hcf_16)_inpw( port );
	if ( log_file ) {
		fprintf( log_file, "\nR %2.2x %4.4x", (port)&0xFF, i);
	}
	return i;
} // in_port_word

#define OUT_PORT_WORD(port, value)  out_port_word( (hcf_io)(port), (hcf_16)(value) )

static void out_port_word( hcf_io port, hcf_16 value ) {
	_outpw( port, value );
	if ( log_file ) {
		fprintf( log_file, "\nW %2.02x %4.04x", (port)&0xFF, value );
	}
}

void IN_PORT_STRING_32( hcf_io prt, hcf_32 FAR * dst, int n)    {
	int i = 0;
	hcf_16 FAR * p;
	if ( log_file ) {
		fprintf( log_file, "\nread string_32 length %04x (%04d) at port %02.2x to addr %lp",
			 (hcf_16)n, (hcf_16)n, (hcf_16)(prt)&0xFF, dst);
	}
	while ( n-- ) {
		p = (hcf_16 FAR *)dst;
		*p++ = (hcf_16)_inpw( prt );
		*p   = (hcf_16)_inpw( prt );
		if ( log_file ) {
			fprintf( log_file, "%s%08lx ", i++ % 0x08 ? " " : "\n", *dst);
		}
		dst++;
	}
} // IN_PORT_STRING_32

void IN_PORT_STRING_8_16( hcf_io prt, hcf_8 FAR * dst, int n) { //also handles byte alignment problems
	hcf_16 FAR * p = (hcf_16 FAR *)dst;                         //this needs more elaborate code in non-x86 platforms
	int i = 0;
	if ( log_file ) {
		fprintf( log_file, "\nread string_16 length %04x (%04d) at port %02.2x to addr %lp",
			 (hcf_16)n, (hcf_16)n, (hcf_16)(prt)&0xFF, dst );
	}
	while ( n-- ) {
		*p =(hcf_16)_inpw( prt);
		if ( log_file ) {
			if ( i++ % 0x10 ) {
				fprintf( log_file, "%04x ", *p);
			} else {
				fprintf( log_file, "\n%04x ", *p);
			}
		}
		p++;
	}
} // IN_PORT_STRING_8_16

void OUT_PORT_STRING_32( hcf_io prt, hcf_32 FAR * src, int n)   {
	int i = 0;
	hcf_16 FAR * p;
	if ( log_file ) {
		fprintf( log_file, "\nwrite string_32 length %04x (%04d) at port %02.2x",
			 (hcf_16)n, (hcf_16)n, (hcf_16)(prt)&0xFF);
	}
	while ( n-- ) {
		p = (hcf_16 FAR *)src;
		_outpw( prt, *p++ );
		_outpw( prt, *p   );
		if ( log_file ) {
			fprintf( log_file, "%s%08lx ", i++ % 0x08 ? " " : "\n", *src);
		}
		src++;
	}
} // OUT_PORT_STRING_32

void OUT_PORT_STRING_8_16( hcf_io prt, hcf_8 FAR * src, int n)  {   //also handles byte alignment problems
	hcf_16 FAR * p = (hcf_16 FAR *)src;                             //this needs more elaborate code in non-x86 platforms
	int i = 0;
	if ( log_file ) {
		fprintf( log_file, "\nwrite string_16 length %04x (%04d) at port %04x", n, n, (hcf_16)prt);
	}
	while ( n-- ) {
		(void)_outpw( prt, *p);
		if ( log_file ) {
			if ( i++ % 0x10 ) {
				fprintf( log_file, "%04x ", *p);
			} else {
				fprintf( log_file, "\n%04x ", *p);
			}
		}
		p++;
	}
} // OUT_PORT_STRING_8_16

#endif // IN_PORT_WORD

/************************************************************************************************************
 ******************************* D A T A    D E F I N I T I O N S ********************************************
 ************************************************************************************************************/

#if HCF_ASSERT
IFBP BASED assert_ifbp = NULL;          //to make asserts easily work under MMD and DHF
#endif // HCF_ASSERT

/* SNAP header to be inserted in Ethernet-II frames */
HCF_STATIC  hcf_8 BASED snap_header[] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, //5 bytes signature +
                                          0 };                          //1 byte protocol identifier

#if (HCF_TYPE) & HCF_TYPE_WPA
HCF_STATIC hcf_8 BASED mic_pad[8] = { 0x5A, 0, 0, 0, 0, 0, 0, 0 };      //MIC padding of message
#endif // HCF_TYPE_WPA

#if defined MSF_COMPONENT_ID
static CFG_IDENTITY_STRCT BASED cfg_drv_identity = {
	sizeof(cfg_drv_identity)/sizeof(hcf_16) - 1,    //length of RID
	CFG_DRV_IDENTITY,           // (0x0826)
	MSF_COMPONENT_ID,
	MSF_COMPONENT_VAR,
	MSF_COMPONENT_MAJOR_VER,
	MSF_COMPONENT_MINOR_VER
} ;

static CFG_RANGES_STRCT BASED cfg_drv_sup_range = {
	sizeof(cfg_drv_sup_range)/sizeof(hcf_16) - 1,   //length of RID
	CFG_DRV_SUP_RANGE,          // (0x0827)

	COMP_ROLE_SUPL,
	COMP_ID_DUI,
	{{  DUI_COMPAT_VAR,
	    DUI_COMPAT_BOT,
	    DUI_COMPAT_TOP
	}}
} ;

static struct CFG_RANGE3_STRCT BASED cfg_drv_act_ranges_pri = {
	sizeof(cfg_drv_act_ranges_pri)/sizeof(hcf_16) - 1,  //length of RID
	CFG_DRV_ACT_RANGES_PRI,     // (0x0828)

	COMP_ROLE_ACT,
	COMP_ID_PRI,
	{
		{ 0, 0, 0 },                           // HCF_PRI_VAR_1 not supported by HCF 7
		{ 0, 0, 0 },                           // HCF_PRI_VAR_2 not supported by HCF 7
		{  3,                                  //var_rec[2] - Variant number
		   CFG_DRV_ACT_RANGES_PRI_3_BOTTOM,        //       - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_PRI_3_TOP            //       - Top Compatibility
		}
	}
} ;


static struct CFG_RANGE4_STRCT BASED cfg_drv_act_ranges_sta = {
	sizeof(cfg_drv_act_ranges_sta)/sizeof(hcf_16) - 1,  //length of RID
	CFG_DRV_ACT_RANGES_STA,     // (0x0829)

	COMP_ROLE_ACT,
	COMP_ID_STA,
	{
#if defined HCF_STA_VAR_1
		{  1,                                  //var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_STA_1_BOTTOM,        //       - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_STA_1_TOP            //       - Top Compatibility
		},
#else
		{ 0, 0, 0 },
#endif // HCF_STA_VAR_1
#if defined HCF_STA_VAR_2
		{  2,                                  //var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_STA_2_BOTTOM,        //       - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_STA_2_TOP            //       - Top Compatibility
		},
#else
		{ 0, 0, 0 },
#endif // HCF_STA_VAR_2
// For Native_USB (Not used!)
#if defined HCF_STA_VAR_3
		{  3,                                  //var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_STA_3_BOTTOM,        //       - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_STA_3_TOP            //       - Top Compatibility
		},
#else
		{ 0, 0, 0 },
#endif // HCF_STA_VAR_3
// Warp
#if defined HCF_STA_VAR_4
		{  4,                                  //var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_STA_4_BOTTOM,        //           - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_STA_4_TOP            //           - Top Compatibility
		}
#else
		{ 0, 0, 0 }
#endif // HCF_STA_VAR_4
	}
} ;


static struct CFG_RANGE6_STRCT BASED cfg_drv_act_ranges_hsi = {
	sizeof(cfg_drv_act_ranges_hsi)/sizeof(hcf_16) - 1,  //length of RID
	CFG_DRV_ACT_RANGES_HSI,     // (0x082A)
	COMP_ROLE_ACT,
	COMP_ID_HSI,
	{
#if defined HCF_HSI_VAR_0                   // Controlled deployment
		{  0,                                  // var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_HSI_0_BOTTOM,        //           - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_HSI_0_TOP            //           - Top Compatibility
		},
#else
		{ 0, 0, 0 },
#endif // HCF_HSI_VAR_0
		{ 0, 0, 0 },                           // HCF_HSI_VAR_1 not supported by HCF 7
		{ 0, 0, 0 },                           // HCF_HSI_VAR_2 not supported by HCF 7
		{ 0, 0, 0 },                           // HCF_HSI_VAR_3 not supported by HCF 7
#if defined HCF_HSI_VAR_4                   // Hermes-II all types
		{  4,                                  // var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_HSI_4_BOTTOM,        //           - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_HSI_4_TOP            //           - Top Compatibility
		},
#else
		{ 0, 0, 0 },
#endif // HCF_HSI_VAR_4
#if defined HCF_HSI_VAR_5                   // WARP Hermes-2.5
		{  5,                                  // var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_HSI_5_BOTTOM,        //           - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_HSI_5_TOP            //           - Top Compatibility
		}
#else
		{ 0, 0, 0 }
#endif // HCF_HSI_VAR_5
	}
} ;


static CFG_RANGE4_STRCT BASED cfg_drv_act_ranges_apf = {
	sizeof(cfg_drv_act_ranges_apf)/sizeof(hcf_16) - 1,  //length of RID
	CFG_DRV_ACT_RANGES_APF,     // (0x082B)

	COMP_ROLE_ACT,
	COMP_ID_APF,
	{
#if defined HCF_APF_VAR_1               //(Fake) Hermes-I
		{  1,                                  //var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_APF_1_BOTTOM,        //           - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_APF_1_TOP            //           - Top Compatibility
		},
#else
		{ 0, 0, 0 },
#endif // HCF_APF_VAR_1
#if defined HCF_APF_VAR_2               //Hermes-II
		{  2,                                  // var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_APF_2_BOTTOM,        //           - Bottom Compatibility
		   CFG_DRV_ACT_RANGES_APF_2_TOP            //           - Top Compatibility
		},
#else
		{ 0, 0, 0 },
#endif // HCF_APF_VAR_2
#if defined HCF_APF_VAR_3                       // Native_USB
		{  3,                                      // var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_APF_3_BOTTOM,        //           - Bottom Compatibility !!!!!see note below!!!!!!!
		   CFG_DRV_ACT_RANGES_APF_3_TOP            //           - Top Compatibility
		},
#else
		{ 0, 0, 0 },
#endif // HCF_APF_VAR_3
#if defined HCF_APF_VAR_4                       // WARP Hermes 2.5
		{  4,                                      // var_rec[1] - Variant number
		   CFG_DRV_ACT_RANGES_APF_4_BOTTOM,        //           - Bottom Compatibility !!!!!see note below!!!!!!!
		   CFG_DRV_ACT_RANGES_APF_4_TOP            //           - Top Compatibility
		}
#else
		{ 0, 0, 0 }
#endif // HCF_APF_VAR_4
	}
} ;
#define HCF_VERSION  TEXT( "HCF$Revision: 1.10 $" )

static struct /*CFG_HCF_OPT_STRCT*/ {
	hcf_16  len;                    //length of cfg_hcf_opt struct
	hcf_16  typ;                    //type 0x082C
	hcf_16   v0;                        //offset HCF_VERSION
	hcf_16   v1;                        // MSF_COMPONENT_ID
	hcf_16   v2;                        // HCF_ALIGN
	hcf_16   v3;                        // HCF_ASSERT
	hcf_16   v4;                        // HCF_BIG_ENDIAN
	hcf_16   v5;                        // /* HCF_DLV | HCF_DLNV */
	hcf_16   v6;                        // HCF_DMA
	hcf_16   v7;                        // HCF_ENCAP
	hcf_16   v8;                        // HCF_EXT
	hcf_16   v9;                        // HCF_INT_ON
	hcf_16  v10;                        // HCF_IO
	hcf_16  v11;                        // HCF_LEGACY
	hcf_16  v12;                        // HCF_MAX_LTV
	hcf_16  v13;                        // HCF_PROT_TIME
	hcf_16  v14;                        // HCF_SLEEP
	hcf_16  v15;                        // HCF_TALLIES
	hcf_16  v16;                        // HCF_TYPE
	hcf_16  v17;                        // HCF_NIC_TAL_CNT
	hcf_16  v18;                        // HCF_HCF_TAL_CNT
	hcf_16  v19;                        // offset tallies
	char    val[sizeof(HCF_VERSION)];
} BASED cfg_hcf_opt = {
	sizeof(cfg_hcf_opt)/sizeof(hcf_16) -1,
	CFG_HCF_OPT,                // (0x082C)
	( sizeof(cfg_hcf_opt) - sizeof(HCF_VERSION) - 4 )/sizeof(hcf_16),
#if defined MSF_COMPONENT_ID
	MSF_COMPONENT_ID,
#else
	0,
#endif // MSF_COMPONENT_ID
	HCF_ALIGN,
	HCF_ASSERT,
	HCF_BIG_ENDIAN,
	0,                                  // /* HCF_DLV | HCF_DLNV*/,
	HCF_DMA,
	HCF_ENCAP,
	HCF_EXT,
	HCF_INT_ON,
	HCF_IO,
	HCF_LEGACY,
	HCF_MAX_LTV,
	HCF_PROT_TIME,
	HCF_SLEEP,
	HCF_TALLIES,
	HCF_TYPE,
#if (HCF_TALLIES) & ( HCF_TALLIES_NIC | HCF_TALLIES_HCF )
	HCF_NIC_TAL_CNT,
	HCF_HCF_TAL_CNT,
	offsetof(IFB_STRCT, IFB_TallyLen ),
#else
	0, 0, 0,
#endif // HCF_TALLIES_NIC / HCF_TALLIES_HCF
	HCF_VERSION
}; // cfg_hcf_opt
#endif // MSF_COMPONENT_ID

HCF_STATIC LTV_STRCT BASED cfg_null = { 1, CFG_NULL, {0} };

HCF_STATIC hcf_16* BASED xxxx[ ] = {
	&cfg_null.len,                          //CFG_NULL                      0x0820
#if defined MSF_COMPONENT_ID
	&cfg_drv_identity.len,                  //CFG_DRV_IDENTITY              0x0826
	&cfg_drv_sup_range.len,                 //CFG_DRV_SUP_RANGE             0x0827
	&cfg_drv_act_ranges_pri.len,            //CFG_DRV_ACT_RANGES_PRI        0x0828
	&cfg_drv_act_ranges_sta.len,            //CFG_DRV_ACT_RANGES_STA        0x0829
	&cfg_drv_act_ranges_hsi.len,            //CFG_DRV_ACT_RANGES_HSI        0x082A
	&cfg_drv_act_ranges_apf.len,            //CFG_DRV_ACT_RANGES_APF        0x082B
	&cfg_hcf_opt.len,                       //CFG_HCF_OPT                   0x082C
	NULL,                                   //IFB_PRIIdentity placeholder   0xFD02
	NULL,                                   //IFB_PRISup placeholder        0xFD03
#endif // MSF_COMPONENT_ID
	NULL                                    //endsentinel
};
#define xxxx_PRI_IDENTITY_OFFSET    (ARRAY_SIZE(xxxx) - 3)


/************************************************************************************************************
 ************************** T O P   L E V E L   H C F   R O U T I N E S **************************************
 ************************************************************************************************************/

/************************************************************************************************************
 *
 *.MODULE        int hcf_action( IFBP ifbp, hcf_16 action )
 *.PURPOSE       Changes the run-time Card behavior.
 *               Performs Miscellanuous actions.
 *
 *.ARGUMENTS
 *   ifbp                    address of the Interface Block
 *   action                  number identifying the type of change
 *    - HCF_ACT_INT_FORCE_ON enable interrupt generation by WaveLAN NIC
 *    - HCF_ACT_INT_OFF      disable interrupt generation by WaveLAN NIC
 *    - HCF_ACT_INT_ON       compensate 1 HCF_ACT_INT_OFF, enable interrupt generation if balance reached
 *    - HCF_ACT_PRS_SCAN     Hermes Probe Response Scan (F102) command
 *    - HCF_ACT_RX_ACK       acknowledge non-DMA receiver to Hermes
 *    - HCF_ACT_SCAN         Hermes Inquire Scan (F101) command (non-WARP only)
 *    - HCF_ACT_SLEEP        DDS Sleep request
 *    - HCF_ACT_TALLIES      Hermes Inquire Tallies (F100) command
 *
 *.RETURNS
 *   HCF_SUCCESS             all (including invalid)
 *   HCF_INT_PENDING         HCF_ACT_INT_OFF, interrupt pending
 *   HCF_ERR_NO_NIC          HCF_ACT_INT_OFF, NIC presence check fails
 *
 *.CONDITIONS
 * Except for hcf_action with HCF_ACT_INT_FORCE_ON or HCF_ACT_INT_OFF as parameter or hcf_connect with an I/O
 * address (i.e. not HCF_DISCONNECT), all hcf-function calls MUST be preceded by a call of hcf_action with
 * HCF_ACT_INT_OFF as parameter.
 * Note that hcf_connect defaults to NIC interrupt disabled mode, i.e. as if hcf_action( HCF_ACT_INT_OFF )
 * was called.
 *
 *.DESCRIPTION
 * hcf_action supports the following mode changing action-code pairs that are antonyms
 *    - HCF_ACT_INT_[FORCE_]ON / HCF_ACT_INT_OFF
 *
 * Additionally hcf_action can start the following actions in the NIC:
 *    - HCF_ACT_PRS_SCAN
 *    - HCF_ACT_RX_ACK
 *    - HCF_ACT_SCAN
 *    - HCF_ACT_SLEEP
 *    - HCF_ACT_TALLIES
 *
 * o HCF_ACT_INT_OFF: Sets NIC Interrupts mode Disabled.
 * This command, and the associated [Force] Enable NIC interrupts command, are only available if the HCF_INT_ON
 * compile time option is not set at 0x0000.
 *
 * o HCF_ACT_INT_ON: Sets NIC Interrupts mode Enabled.
 * Enable NIC Interrupts, depending on the number of preceding Disable NIC Interrupt calls.
 *
 * o HCF_ACT_INT_FORCE_ON: Force NIC Interrupts mode Enabled.
 * Sets NIC Interrupts mode Enabled, regardless off the number of preceding Disable NIC Interrupt calls.
 *
 * The disabling and enabling of interrupts are antonyms.
 * These actions must be balanced.
 * For each "disable interrupts" there must be a matching "enable interrupts".
 * The disable interrupts may be executed multiple times in a row without intervening enable interrupts, in
 * other words, the disable interrupts may be nested.
 * The interrupt generation mechanism is disabled at the first call with HCF_ACT_INT_OFF.
 * The interrupt generation mechanism is re-enabled when the number of calls with HCF_ACT_INT_ON matches the
 * number of calls with INT_OFF.
 *
 * It is not allowed to have more Enable NIC Interrupts calls than Disable NIC Interrupts calls.
 * The interrupt generation mechanism is initially (i.e. after hcf_connect) disabled.
 * An MSF based on a interrupt strategy must call hcf_action with INT_ON in its initialization logic.
 *
 *!  The INT_OFF/INT_ON housekeeping is initialized at 0x0000 by hcf_connect, causing the interrupt generation
 *   mechanism to be disabled at first. This suits MSF implementation based on a polling strategy.
 *
 * o HCF_ACT_SLEEP: Initiates the Disconnected DeepSleep process
 * This command is only available if the HCF_DDS compile time option is set. It triggers the F/W to start the
 * sleep handshaking. Regardless whether the Host initiates a Disconnected DeepSleep (DDS) or the F/W initiates
 * a Connected DeepSleep (CDS), the Host-F/W sleep handshaking is completed when the NIC Interrupts mode is
 * enabled (by means of the balancing HCF_ACT_INT_ON), i.e. at that moment the F/W really goes into sleep mode.
 * The F/W is wokenup by the HCF when the NIC Interrupts mode are disabled, i.e. at the first HCF_ACT_INT_OFF
 * after going into sleep.
 *
 * The following Miscellaneous actions are defined:
 *
 * o HCF_ACT_RX_ACK: Receiver Acknowledgement (non-DMA, non-USB mode only)
 * Acking the receiver, frees the NIC memory used to hold the Rx frame and allows the F/W to
 * report the existence of the next Rx frame.
 * If the MSF does not need access (any longer) to the current frame, e.g. because it is rejected based on the
 * look ahead or copied to another buffer, the receiver may be acked. Acking earlier is assumed to have the
 * potential of improving the performance.
 * If the MSF does not explicitly ack the receiver, the acking is done implicitly if:
 * - the received frame fits in the look ahead buffer, by the hcf_service_nic call that reported the Rx frame
 * - if not in the above step, by hcf_rcv_msg (assuming hcf_rcv_msg is called)
 * - if neither of the above implicit acks nor an explicit ack by the MSF, by the first hcf_service_nic after
 *   the hcf_service_nic that reported the Rx frame.
 * Note: If an Rx frame is already acked, an explicit ACK by the MSF acts as a NoOperation.
 *
 * o HCF_ACT_TALLIES: Inquire Tallies command
 * This command is only operational if the F/W is enabled.
 * The Inquire Tallies command requests the F/W to provide its current set of tallies.
 * See also hcf_get_info with CFG_TALLIES as parameter.
 *
 * o HCF_ACT_PRS_SCAN: Inquire Probe Response Scan command
 * This command is only operational if the F/W is enabled.
 * The Probe Response Scan command starts a scan sequence.
 * The HCF puts the result of this action in an MSF defined buffer (see CFG_RID_LOG_STRCT).
 *
 * o HCF_ACT_SCAN: Inquire Scan command
 * This command is only supported for HII F/W (i.e. pre-WARP) and it is operational if the F/W is enabled.
 * The Inquire Scan command starts a scan sequence.
 * The HCF puts the result of this action in an MSF defined buffer (see CFG_RID_LOG_STRCT).
 *
 * Assert fails if
 * - ifbp has a recognizable out-of-range value.
 * - NIC interrupts are not disabled while required by parameter action.
 * - an invalid code is specified in parameter action.
 * - HCF_ACT_INT_ON commands outnumber the HCF_ACT_INT_OFF commands.
 * - reentrancy, may be caused by calling hcf_functions without adequate protection against NIC interrupts or
 *   multi-threading
 *
 * - Since the HCF does not maintain status information relative to the F/W enabled state, it is not asserted
 *   whether HCF_ACT_SCAN, HCF_ACT_PRS_SCAN or HCF_ACT_TALLIES are only used while F/W is enabled.
 *
 *.DIAGRAM
 * 0: The assert embedded in HCFLOGENTRY checks against re-entrancy. Re-entrancy could be caused by a MSF logic
 *   at task-level calling hcf_functions without shielding with HCF_ACT_ON/_OFF. However the HCF_ACT_INT_OFF
 *   action itself can per definition not be protected this way. Based on code inspection, it can be concluded,
 *   that there is no re-entrancy PROBLEM in this particular flow. It does not seem worth the trouble to
 *   explicitly check for this condition (although there was a report of an MSF which ran into this assert.
 * 2:IFB_IntOffCnt is used to balance the INT_OFF and INT_ON calls.  Disabling of the interrupts is achieved by
 *   writing a zero to the Hermes IntEn register.  In a shared interrupt environment (e.g. the mini-PCI NDIS
 *   driver) it is considered more correct to return the status HCF_INT_PENDING if and only if, the current
 *   invocation of hcf_service_nic is (apparently) called in the ISR when the ISR was activated as result of a
 *   change in HREG_EV_STAT matching a bit in HREG_INT_EN, i.e. not if invoked as result of another device
 *   generating an interrupt on the shared interrupt line.
 *   Note 1: it has been observed that under certain adverse conditions on certain platforms the writing of
 *   HREG_INT_EN can apparently fail, therefore it is paramount that HREG_INT_EN is written again with 0 for
 *   each and every call to HCF_ACT_INT_OFF.
 *   Note 2: it has been observed that under certain H/W & S/W architectures this logic is called when there is
 *   no NIC at all. To cater for this, the value of HREG_INT_EN is validated. If the unused bit 0x0100 is set,
 *   it is assumed there is no NIC.
 *   Note 3: During the download process, some versions of the F/W reset HREG_SW_0, hence checking this
 *   register for HCF_MAGIC (the classical NIC presence test) when HCF_ACT_INT_OFF is called due to another
 *   card interrupting via a shared IRQ during a download, fails.
 *4: The construction "if ( ifbp->IFB_IntOffCnt-- == 0 )" is optimal (in the sense of shortest/quickest
 *   path in error free flows) but NOT fail safe in case of too many INT_ON invocations compared to INT_OFF).
 *   Enabling of the interrupts is achieved by writing the Hermes IntEn register.
 *    - If the HCF is in Defunct mode, the interrupts stay disabled.
 *    - Under "normal" conditions, the HCF is only interested in Info Events, Rx Events and Notify Events.
 *    - When the HCF is out of Tx/Notify resources, the HCF is also interested in Alloc Events.
 *    - via HCF_EXT, the MSF programmer can also request HREG_EV_TICK and/or HREG_EV_TX_EXC interrupts.
 *   For DMA operation, the DMA hardware handles the alloc events. The DMA engine will generate a 'TxDmaDone'
 *   event as soon as it has pumped a frame from host ram into NIC-RAM (note that the frame does not have to be
 *   transmitted then), and a 'RxDmaDone' event as soon as a received frame has been pumped from NIC-RAM into
 *   host ram.  Note that the 'alloc' event has been removed from the event-mask, because the DMA engine will
 *   react to and acknowledge this event.
 *6: ack the "old" Rx-event. See "Rx Buffer free strategy" in hcf_service_nic above for more explanation.
 *   IFB_RxFID and IFB_RxLen must be cleared to bring both the internal HCF house keeping and the information
 *   supplied to the MSF in the state "no frame received".
 *8: The HCF_ACT_SCAN, HCF_ACT_PRS_SCAN and HCF_ACT_TALLIES activity are merged by "clever" algebraic
 *   manipulations of the RID-values and action codes, so foregoing robustness against migration problems for
 *   ease of implementation. The assumptions about numerical relationships between CFG_TALLIES etc and
 *   HCF_ACT_TALLIES etc are checked by the "#if" statements just prior to the body of this routine, resulting
 *   in: err "maintenance" during compilation if the assumptions are no longer met. The writing of HREG_PARAM_1
 *   with 0x3FFF in case of an PRS scan, is a kludge to get around lack of specification, hence different
 *   implementation in F/W and Host.
 *   When there is no NIC RAM available, some versions of the Hermes F/W do report 0x7F00 as error in the
 *   Result field of the Status register and some F/W versions don't. To mask this difference to the MSF all
 *   return codes of the Hermes are ignored ("best" and "most simple" solution to these types of analomies with
 *   an acceptable loss due to ignoring all error situations as well).
 *   The "No inquire space" is reported via the Hermes tallies.
 *30: do not HCFASSERT( rc, rc ) since rc == HCF_INT_PENDING is no error
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
#if ( (HCF_TYPE) & HCF_TYPE_HII5 ) == 0
#if CFG_SCAN != CFG_TALLIES - HCF_ACT_TALLIES + HCF_ACT_SCAN
err: "maintenance" apparently inviolated the underlying assumption about the numerical values of these macros
#endif
#endif // HCF_TYPE_HII5
#if CFG_PRS_SCAN != CFG_TALLIES - HCF_ACT_TALLIES + HCF_ACT_PRS_SCAN
err: "maintenance" apparently inviolated the underlying assumption about the numerical values of these macros
#endif
int
hcf_action( IFBP ifbp, hcf_16 action )
{
	int rc = HCF_SUCCESS;

	HCFASSERT( ifbp->IFB_Magic == HCF_MAGIC, ifbp->IFB_Magic );
#if HCF_INT_ON
	HCFLOGENTRY( action == HCF_ACT_INT_FORCE_ON ? HCF_TRACE_ACTION_KLUDGE : HCF_TRACE_ACTION, action );                                                      /* 0 */
#if (HCF_SLEEP)
	HCFASSERT( ifbp->IFB_IntOffCnt != 0xFFFE || action == HCF_ACT_INT_OFF,
		   MERGE_2( action, ifbp->IFB_IntOffCnt ) );
#else
	HCFASSERT( ifbp->IFB_IntOffCnt != 0xFFFE, action );
#endif // HCF_SLEEP
	HCFASSERT( ifbp->IFB_IntOffCnt != 0xFFFF ||
		   action == HCF_ACT_INT_OFF || action == HCF_ACT_INT_FORCE_ON,  action );
	HCFASSERT( ifbp->IFB_IntOffCnt <= 16 || ifbp->IFB_IntOffCnt >= 0xFFFE,
		   MERGE_2( action, ifbp->IFB_IntOffCnt ) ); //nesting more than 16 deep seems unreasonable
#endif // HCF_INT_ON

	switch (action) {
#if HCF_INT_ON
		hcf_16  i;
	case HCF_ACT_INT_OFF:                     // Disable Interrupt generation
#if HCF_SLEEP
		if ( ifbp->IFB_IntOffCnt == 0xFFFE ) {  // WakeUp test  ;?tie this to the "new" super-LinkStat
			ifbp->IFB_IntOffCnt++;                      // restore conventional I/F
			OPW(HREG_IO, HREG_IO_WAKEUP_ASYNC );        // set wakeup bit
			OPW(HREG_IO, HREG_IO_WAKEUP_ASYNC );        // set wakeup bit to counteract the clearing by F/W
			// 800 us latency before FW switches to high power
			MSF_WAIT(800);                              // MSF-defined function to wait n microseconds.
//OOR			if ( ifbp->IFB_DSLinkStat & CFG_LINK_STAT_DS_OOR ) { // OutOfRange
//				printk(KERN_NOTICE "ACT_INT_OFF: Deepsleep phase terminated, enable and go to AwaitConnection\n" );     //;?remove me 1 day
//				hcf_cntl( ifbp, HCF_CNTL_ENABLE );
//			}
//			ifbp->IFB_DSLinkStat &= ~( CFG_LINK_STAT_DS_IR | CFG_LINK_STAT_DS_OOR); //clear IR/OOR state
		}
#endif // HCF_SLEEP
	/*2*/   ifbp->IFB_IntOffCnt++;
//!		rc = 0;
		i = IPW( HREG_INT_EN );
		OPW( HREG_INT_EN, 0 );
		if ( i & 0x1000 ) {
			rc = HCF_ERR_NO_NIC;
		} else {
			if ( i & IPW( HREG_EV_STAT ) ) {
				rc = HCF_INT_PENDING;
			}
		}
		break;

	case HCF_ACT_INT_FORCE_ON:                // Enforce Enable Interrupt generation
		ifbp->IFB_IntOffCnt = 0;
		//Fall through in HCF_ACT_INT_ON

	case HCF_ACT_INT_ON:                      // Enable Interrupt generation
	/*4*/   if ( ifbp->IFB_IntOffCnt-- == 0 && ifbp->IFB_CardStat == 0 ) {
			                          //determine Interrupt Event mask
#if HCF_DMA
			if ( ifbp->IFB_CntlOpt & USE_DMA ) {
				i = HREG_EV_INFO | HREG_EV_RDMAD | HREG_EV_TDMAD | HREG_EV_TX_EXT;  //mask when DMA active
			} else
#endif // HCF_DMA
			{
				i = HREG_EV_INFO | HREG_EV_RX | HREG_EV_TX_EXT;                     //mask when DMA not active
				if ( ifbp->IFB_RscInd == 0 ) {
					i |= HREG_EV_ALLOC;                                         //mask when no TxFID available
				}
			}
#if HCF_SLEEP
			if ( ( IPW(HREG_EV_STAT) & ( i | HREG_EV_SLEEP_REQ ) ) == HREG_EV_SLEEP_REQ ) {
				// firmware indicates it would like to go into sleep modus
				// only acknowledge this request if no other events that can cause an interrupt are pending
				ifbp->IFB_IntOffCnt--;          //becomes 0xFFFE
				OPW( HREG_INT_EN, i | HREG_EV_TICK );
				OPW( HREG_EV_ACK, HREG_EV_SLEEP_REQ | HREG_EV_TICK | HREG_EV_ACK_REG_READY );
			} else
#endif // HCF_SLEEP
			{
				OPW( HREG_INT_EN, i | HREG_EV_SLEEP_REQ );
			}
		}
		break;
#endif // HCF_INT_ON

#if (HCF_SLEEP) & HCF_DDS
	case HCF_ACT_SLEEP:                       // DDS Sleep request
		hcf_cntl( ifbp, HCF_CNTL_DISABLE );
		cmd_exe( ifbp, HCMD_SLEEP, 0 );
		break;
//	case HCF_ACT_WAKEUP:                      // DDS Wakeup request
//		HCFASSERT( ifbp->IFB_IntOffCnt == 0xFFFE, ifbp->IFB_IntOffCnt );
//		ifbp->IFB_IntOffCnt++;                  // restore conventional I/F
//		OPW( HREG_IO, HREG_IO_WAKEUP_ASYNC );
//		MSF_WAIT(800);                          // MSF-defined function to wait n microseconds.
//		rc = hcf_action( ifbp, HCF_ACT_INT_OFF );   /*bogus, IFB_IntOffCnt == 0xFFFF, so if you carefully look
//		                                             *at the #if HCF_DDS statements, HCF_ACT_INT_OFF is empty
//		                                             *for DDS. "Much" better would be to merge the flows for
//		                                             *DDS and DEEP_SLEEP
//		                                             */
//		break;
#endif // HCF_DDS

	case HCF_ACT_RX_ACK:                      //Receiver ACK
	/*6*/   if ( ifbp->IFB_RxFID ) {
			DAWA_ACK( HREG_EV_RX );
		}
		ifbp->IFB_RxFID = ifbp->IFB_RxLen = 0;
		break;

  /*8*/ case  HCF_ACT_PRS_SCAN:                   // Hermes PRS Scan (F102)
		OPW( HREG_PARAM_1, 0x3FFF );
		//Fall through in HCF_ACT_TALLIES
	case HCF_ACT_TALLIES:                     // Hermes Inquire Tallies (F100)
#if ( (HCF_TYPE) & HCF_TYPE_HII5 ) == 0
	case HCF_ACT_SCAN:                        // Hermes Inquire Scan (F101)
#endif // HCF_TYPE_HII5
		/*!! the assumptions about numerical relationships between CFG_TALLIES etc and HCF_ACT_TALLIES etc
		 *   are checked by #if statements just prior to this routine resulting in: err "maintenance"   */
		cmd_exe( ifbp, HCMD_INQUIRE, action - HCF_ACT_TALLIES + CFG_TALLIES );
		break;

	default:
		HCFASSERT( DO_ASSERT, action );
		break;
	}
	//! do not HCFASSERT( rc == HCF_SUCCESS, rc )                                                       /* 30*/
	HCFLOGEXIT( HCF_TRACE_ACTION );
	return rc;
} // hcf_action


/************************************************************************************************************
 *
 *.MODULE        int hcf_cntl( IFBP ifbp, hcf_16 cmd )
 *.PURPOSE       Connect or disconnect a specific port to a specific network.
 *!!  ;???????????????? continue needs more explanation
 *               recovers by means of "continue" when the connect process in CCX mode fails
 *               Enables or disables data transmission and reception for the NIC.
 *               Activates static NIC configuration for a specific port at connect.
 *               Activates static configuration for all ports at enable.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   cmd         0x001F: Hermes command (disable, enable, connect, disconnect, continue)
 *                   HCF_CNTL_ENABLE     Enable
 *                   HCF_CNTL_DISABLE    Disable
 *                   HCF_CNTL_CONTINUE   Continue
 *                   HCF_CNTL_CONNECT    Connect
 *                   HCF_CNTL_DISCONNECT Disconnect
 *               0x0100: command qualifier (continue)
 *                   HCMD_RETRY          retry flag
 *               0x0700:  port number (connect/disconnect)
 *                   HCF_PORT_0          MAC Port 0
 *                   HCF_PORT_1          MAC Port 1
 *                   HCF_PORT_2          MAC Port 2
 *                   HCF_PORT_3          MAC Port 3
 *                   HCF_PORT_4          MAC Port 4
 *                   HCF_PORT_5          MAC Port 5
 *                   HCF_PORT_6          MAC Port 6
 *
 *.RETURNS
 *   HCF_SUCCESS
 *!! via cmd_exe
 *   HCF_ERR_NO_NIC
 *   HCF_ERR_DEFUNCT_...
 *   HCF_ERR_TIME_OUT
 *
 *.DESCRIPTION
 * The parameter cmd contains a number of subfields.
 * The actual value for cmd is created by logical or-ing the appropriate mnemonics for the subfields.
 * The field 0x001F contains the command code
 *  - HCF_CNTL_ENABLE
 *  - HCF_CNTL_DISABLE
 *  - HCF_CNTL_CONNECT
 *  - HCF_CNTL_DISCONNECT
 *  - HCF_CNTL_CONTINUE
 *
 * For HCF_CNTL_CONTINUE, the field 0x0100 contains the retry flag HCMD_RETRY.
 * For HCF_CNTL_CONNECT and HCF_CNTL_DISCONNECT, the field 0x0700 contains the port number as HCF_PORT_#.
 * For Station as well as AccessPoint F/W, MAC Port 0 is the "normal" communication channel.
 * For AccessPoint F/W, MAC Port 1 through 6 control the WDS links.
 *
 * Note that despite the names HCF_CNTL_DISABLE and HCF_CNTL_ENABLE, hcf_cntl does not influence the NIC
 * Interrupts mode.
 *
 * The Connect is used by the MSF to bring a particular port in an inactive state as far as data transmission
 * and reception are concerned.
 * When a particular port is disconnected:
 * - the F/W disables the receiver for that port.
 * - the F/W ignores send commands for that port.
 * - all frames (Receive as well as pending Transmit) for that port on the NIC are discarded.
 *
 * When the NIC is disabled, above list applies to all ports, i.e. the result is like all ports are
 * disconnected.
 *
 * When a particular port is connected:
 * - the F/W effectuates the static configuration for that port.
 * - enables the receiver for that port.
 * - accepts send commands for that port.
 *
 * Enabling has the following effects:
 * - the F/W effectuates the static configuration for all ports.
 *   The F/W only updates its static configuration at a transition from disabled to enabled or from
 *   disconnected to connected.
 *   In order to enforce the static configuration, the MSF must assure that such a transition takes place.
 *   Due to such a disable/enable or disconnect/connect sequence, Rx/Tx frames may be lost, in other words,
 *   configuration may impact communication.
 * - The DMA Engine (if applicable) is enabled.
 * Note that the Enable Function by itself only enables data transmission and reception, it
 * does not enable the Interrupt Generation mechanism. This is done by hcf_action.
 *
 * Disabling has the following effects:
 *!!  ;?????is the following statement really true
 * - it acts as a disconnect on all ports.
 * - The DMA Engine (if applicable) is disabled.
 *
 * For impact of the disable command on the behavior of hcf_dma_tx/rx_get see the appropriate sections.
 *
 * Although the Enable/Disable and Connect/Disconnect are antonyms, there is no restriction on their sequencing,
 * in other words, they may be called multiple times in arbitrary sequence without being paired or balanced.
 * Each time one of these functions is called, the effects of the preceding calls cease.
 *
 * Assert fails if
 * - ifbp has a recognizable out-of-range value.
 * - NIC interrupts are not disabled.
 * - A command other than Continue, Enable, Disable, Connect or Disconnect is given.
 * - An invalid combination of the subfields is given or a bit outside the subfields is given.
 * - any return code besides HCF_SUCCESS.
 * - reentrancy, may be caused by calling a hcf_function without adequate protection against NIC interrupts or
 *   multi-threading
 *
 *.DIAGRAM
 *   hcf_cntl takes successively the following actions:
 *2: If the HCF is in Defunct mode or incompatible with the Primary or Station Supplier in the Hermes,
 *   hcf_cntl() returns immediately with HCF_ERR_NO_NIC;? as status.
 *8: when the port is disabled, the DMA engine needs to be de-activated, so the host can safely reclaim tx
 *   packets from the tx descriptor chain.
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
int
hcf_cntl( IFBP ifbp, hcf_16 cmd )
{
	int rc = HCF_ERR_INCOMP_FW;
#if HCF_ASSERT
	{   int x = cmd & HCMD_CMD_CODE;
		if ( x == HCF_CNTL_CONTINUE ) x &= ~HCMD_RETRY;
		else if ( (x == HCMD_DISABLE || x == HCMD_ENABLE) && ifbp->IFB_FWIdentity.comp_id == COMP_ID_FW_AP ) {
			x &= ~HFS_TX_CNTL_PORT;
		}
		HCFASSERT( x==HCF_CNTL_ENABLE  || x==HCF_CNTL_DISABLE    || HCF_CNTL_CONTINUE ||
			   x==HCF_CNTL_CONNECT || x==HCF_CNTL_DISCONNECT, cmd );
	}
#endif // HCF_ASSERT
// #if (HCF_SLEEP) & HCF_DDS
//	HCFASSERT( ifbp->IFB_IntOffCnt != 0xFFFE, cmd );
// #endif // HCF_DDS
	HCFLOGENTRY( HCF_TRACE_CNTL, cmd );
	if ( ifbp->IFB_CardStat == 0 ) {                                                                 /*2*/
	/*6*/   rc = cmd_exe( ifbp, cmd, 0 );
#if (HCF_SLEEP) & HCF_DDS
		ifbp->IFB_TickCnt = 0;              //start 2 second period (with 1 tick uncertanty)
#endif // HCF_DDS
	}
#if HCF_DMA
	//!rlav : note that this piece of code is always executed, regardless of the DEFUNCT bit in IFB_CardStat.
	// The reason behind this is that the MSF should be able to get all its DMA resources back from the HCF,
	// even if the hardware is disfunctional. Practical example under Windows : surprise removal.
	if ( ifbp->IFB_CntlOpt & USE_DMA ) {
		hcf_io io_port = ifbp->IFB_IOBase;
		DESC_STRCT *p;
		if ( cmd == HCF_CNTL_DISABLE || cmd == HCF_CNTL_ENABLE ) {
			OUT_PORT_DWORD( (io_port + HREG_DMA_CTRL), DMA_CTRLSTAT_RESET);                     /*8*/
			ifbp->IFB_CntlOpt &= ~DMA_ENABLED;
		}
		if ( cmd == HCF_CNTL_ENABLE ) {
			OUT_PORT_DWORD( (io_port + HREG_DMA_CTRL), DMA_CTRLSTAT_GO);
			/* ;? by rewriting hcf_dma_rx_put you can probably just call hcf_dma_rx_put( ifbp->IFB_FirstDesc[DMA_RX] )
			 * as additional beneficiary side effect, the SOP and EOP bits will also be cleared
			 */
			ifbp->IFB_CntlOpt |= DMA_ENABLED;
			HCFASSERT( NT_ASSERT, NEVER_TESTED );
			// make the entire rx descriptor chain DMA-owned, so the DMA engine can (re-)use it.
			p = ifbp->IFB_FirstDesc[DMA_RX];
			if (p != NULL) {   //;? Think this over again in the light of the new chaining strategy
				if ( 1 )    { //begin alternative
					HCFASSERT( NT_ASSERT, NEVER_TESTED );
					put_frame_lst( ifbp, ifbp->IFB_FirstDesc[DMA_RX], DMA_RX );
					if ( ifbp->IFB_FirstDesc[DMA_RX] ) {
						put_frame_lst( ifbp, ifbp->IFB_FirstDesc[DMA_RX]->next_desc_addr, DMA_RX );
					}
				} else {
					while ( p ) {
						//p->buf_cntl.cntl_stat |= DESC_DMA_OWNED;
						p->BUF_CNT |= DESC_DMA_OWNED;
						p = p->next_desc_addr;
					}
					// a rx chain is available so hand it over to the DMA engine
					p = ifbp->IFB_FirstDesc[DMA_RX];
					OUT_PORT_DWORD( (io_port + HREG_RXDMA_PTR32), p->desc_phys_addr);
				}  //end alternative
			}
		}
	}
#endif // HCF_DMA
	HCFASSERT( rc == HCF_SUCCESS, rc );
	HCFLOGEXIT( HCF_TRACE_CNTL );
	return rc;
} // hcf_cntl


/************************************************************************************************************
 *
 *.MODULE        int hcf_connect( IFBP ifbp, hcf_io io_base )
 *.PURPOSE       Grants access right for the HCF to the IFB.
 *               Initializes Card and HCF housekeeping.
 *
 *.ARGUMENTS
 *   ifbp        (near) address of the Interface Block
 *   io_base     non-USB: I/O Base address of the NIC (connect)
 *               non-USB: HCF_DISCONNECT
 *               USB:     HCF_CONNECT, HCF_DISCONNECT
 *
 *.RETURNS
 *   HCF_SUCCESS
 *   HCF_ERR_INCOMP_PRI
 *   HCF_ERR_INCOMP_FW
 *   HCF_ERR_DEFUNCT_CMD_SEQ
 *!! HCF_ERR_NO_NIC really returned ;?
 *   HCF_ERR_NO_NIC
 *   HCF_ERR_TIME_OUT
 *
 *   MSF-accessible fields of Result Block:
 *   IFB_IOBase              entry parameter io_base
 *   IFB_IORange             HREG_IO_RANGE (0x40/0x80)
 *   IFB_Version             version of the IFB layout
 *   IFB_FWIdentity          CFG_FW_IDENTITY_STRCT, specifies the identity of the
 *                           "running" F/W, i.e. tertiary F/W under normal conditions
 *   IFB_FWSup               CFG_SUP_RANGE_STRCT, specifies the supplier range of
 *                           the "running" F/W, i.e. tertiary F/W under normal conditions
 *   IFB_HSISup              CFG_SUP_RANGE_STRCT, specifies the HW/SW I/F range of the NIC
 *   IFB_PRIIdentity         CFG_PRI_IDENTITY_STRCT, specifies the Identity of the Primary F/W
 *   IFB_PRISup              CFG_SUP_RANGE_STRCT, specifies the supplier range of the Primary F/W
 *   all other               all MSF accessible fields, which are not specified above, are zero-filled
 *
 *.CONDITIONS
 * It is the responsibility of the MSF to assure the correctness of the I/O Base address.
 *
 * Note: hcf_connect defaults to NIC interrupt disabled mode, i.e. as if hcf_action( HCF_ACT_INT_OFF )
 * was called.
 *
 *.DESCRIPTION
 * hcf_connect passes the MSF-defined location of the IFB to the HCF and grants or revokes access right for the
 * HCF to the IFB. Revoking is done by specifying HCF_DISCONNECT rather than an I/O address for the parameter
 * io_base.  Every call of hcf_connect in "connect" mode, must eventually be followed by a call of hcf_connect
 * in "disconnect" mode. Calling hcf_connect in "connect"/"disconnect" mode can not be nested.
 * The IFB address must be used as a handle with all subsequent HCF-function calls and the HCF uses the IFB
 * address as a handle when it performs a call(back) of an MSF-function (i.e. msf_assert).
 *
 * Note that not only the MSF accessible fields are cleared, but also all internal housekeeping
 * information is re-initialized.
 * This implies that all settings which are done via hcf_action and hcf_put_info (e.g. CFG_MB_ASSERT, CFG_REG_MB,
 * CFG_REG_INFO_LOG) must be done again. The only field which is not cleared, is IFB_MSFSup.
 *
 * If HCF_INT_ON is selected as compile option, NIC interrupts are disabled.
 *
 * Assert fails if
 * - ifbp is not properly aligned ( ref chapter HCF_ALIGN in 4.1.1)
 * - I/O Base Address is not a multiple of 0x40 (note: 0x0000 is explicitly allowed).
 *
 *.DIAGRAM
 *
 *0: Throughout hcf_connect you need to distinguish the connect from the disconnect case, which requires
 *   some attention about what to use as "I/O" address when for which purpose.
 *2:
 *2a: Reset H-II by toggling reset bit in IO-register on and off.
 *   The HCF_TYPE_PRELOADED caters for the DOS environment where H-II is loaded by a separate program to
 *   overcome the 64k size limit posed on DOS drivers.
 *   The macro OPW is not yet useable because the IFB_IOBase field is not set.
 *   Note 1: hopefully the clearing and initializing of the IFB (see below) acts as a delay which meets the
 *   specification for S/W reset
 *   Note 2: it turns out that on some H/W constellations, the clock to access the EEProm is not lowered
 *   to an appropriate frequency by HREG_IO_SRESET. By giving an HCMD_INI first, this problem is worked around.
 *2b: Experimentally it is determined over a wide range of F/W versions that are waiting for the for Cmd bit in
 *   Ev register gives a workable strategy. The available documentation does not give much clues.
 *4: clear and initialize the IFB
 *   The HCF house keeping info is designed such that zero is the appropriate initial value for as much as
 *   feasible IFB-items.
 *   The readable fields mentioned in the description section and some HCF specific fields are given their
 *   actual value.
 *   IFB_TickIni is initialized at best guess before calibration
 *   Hcf_connect defaults to "no interrupt generation" (implicitly achieved by the zero-filling).
 *6: Register compile-time linked MSF Routine and set default filter level
 *   cast needed to get around the "near" problem in DOS COM model
 *   er C2446: no conversion from void (__near __cdecl *)(unsigned char __far *,unsigned int,unsigned short,int)
 *                           to   void (__far  __cdecl *)(unsigned char __far *,unsigned int,unsigned short,int)
 *8: If a command is apparently still active (as indicated by the Busy bit in Cmd register) this may indicate a
 *   blocked cmd pipe line.  To unblock the following actions are done:
 *    - Ack everything
 *    - Wait for Busy bit drop  in Cmd register
 *    - Wait for Cmd  bit raise in Ev  register
 *   The two waits are combined in a single HCF_WAIT_WHILE to optimize memory size. If either of these waits
 *   fail (prot_cnt becomes 0), then something is serious wrong. Rather than PANICK, the assumption is that the
 *   next cmd_exe will fail, causing the HCF to go into DEFUNCT mode
 *10:    Ack everything to unblock a (possibly blocked) cmd pipe line
 *   Note 1: it is very likely that an Alloc event is pending and very well possible that a (Send) Cmd event is
 *   pending on non-initial calls
 *   Note 2: it is assumed that this strategy takes away the need to ack every conceivable event after an
 *   Hermes Initialize
 *12:    Only H-II NEEDS the Hermes Initialize command. Due to the different semantics for H-I and H-II
 *   Initialize command, init() does not (and can not, since it is called e.g. after a download) execute the
 *   Hermes Initialize command. Executing the Hermes Initialize command for H-I would not harm but not do
 *   anything useful either, so it is skipped.
 *   The return status of cmd_exe is ignored. It is assumed that if cmd_exe fails, init fails too
 *14:    use io_base as a flag to merge hcf_connect and hcf_disconnect into 1 routine
 *   the call to init and its subsequent call of cmd_exe will return HCF_ERR_NO_NIC if appropriate. This status
 *   is (badly) needed by some legacy combination of NT4 and card services which do not yield an I/O address in
 *   time.
 *
 *.NOTICE
 *   On platforms where the NULL-pointer is not a bit-pattern of all zeros, the zero-filling of the IFB results
 *   in an incorrect initialization of pointers.
 *   The implementation of the MailBox manipulation in put_mb_info protects against the absence of a MailBox
 *   based on IFB_MBSize, IFB_MBWp and ifbp->IFB_MBRp. This has ramifications on the initialization of the
 *   MailBox via hcf_put_info with the CFG_REG_MB type, but it prevents dependency on the "NULL-"ness of
 *   IFB_MBp.
 *
 *.NOTICE
 *   There are a number of problems when asserting and logging hcf_connect, e.g.
 *    - Asserting on re-entrancy of hcf_connect by means of
 *    "HCFASSERT( (ifbp->IFB_AssertTrace & HCF_ASSERT_CONNECT) == 0, 0 )" is not useful because IFB contents
 *    are undefined
 *    - Asserting before the IFB is cleared will cause mdd_assert() to interpret the garbage in IFB_AssertRtn
 *    as a routine address
 *   Therefore HCFTRACE nor HCFLOGENTRY is called by hcf_connect.
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
int
hcf_connect( IFBP ifbp, hcf_io io_base )
{
	int         rc = HCF_SUCCESS;
	hcf_io      io_addr;
	hcf_32      prot_cnt;
	hcf_8       *q;
	LTV_STRCT   x;
#if HCF_ASSERT
	hcf_16 xa = ifbp->IFB_FWIdentity.typ;
	/* is assumed to cause an assert later on if hcf_connect is called without intervening hcf_disconnect.
	 * xa == CFG_FW_IDENTITY in subsequent calls without preceding hcf_disconnect,
	 * xa == 0 in subsequent calls with preceding hcf_disconnect,
	 * xa == "garbage" (any value except CFG_FW_IDENTITY is acceptable) in the initial call
	 */
#endif // HCF_ASSERT

	if ( io_base == HCF_DISCONNECT ) {                  //disconnect
		io_addr = ifbp->IFB_IOBase;
		OPW( HREG_INT_EN, 0 );      //;?workaround against dying F/W on subsequent hcf_connect calls
	} else {                                            //connect                               /* 0 */
		io_addr = io_base;
	}

#if 0 //;? if a subsequent hcf_connect is preceded by an hcf_disconnect the wakeup is not needed !!
#if HCF_SLEEP
	OUT_PORT_WORD( .....+HREG_IO, HREG_IO_WAKEUP_ASYNC );       //OPW not yet useable
	MSF_WAIT(800);                              // MSF-defined function to wait n microseconds.
	note that MSF_WAIT uses not yet defined!!!! IFB_IOBase and IFB_TickIni (via PROT_CNT_INI)
	so be careful if this code is restored
#endif // HCF_SLEEP
#endif // 0

#if ( (HCF_TYPE) & HCF_TYPE_PRELOADED ) == 0    //switch clock back for SEEPROM access  !!!
	OUT_PORT_WORD( io_addr + HREG_CMD, HCMD_INI );          //OPW not yet useable
	prot_cnt = INI_TICK_INI;
	HCF_WAIT_WHILE( (IN_PORT_WORD( io_addr +  HREG_EV_STAT) & HREG_EV_CMD) == 0 );
	OUT_PORT_WORD( (io_addr + HREG_IO), HREG_IO_SRESET );   //OPW not yet useable                   /* 2a*/
#endif // HCF_TYPE_PRELOADED
	for ( q = (hcf_8*)(&ifbp->IFB_Magic); q > (hcf_8*)ifbp; *--q = 0 ) /*NOP*/;                     /* 4 */
	ifbp->IFB_Magic     = HCF_MAGIC;
	ifbp->IFB_Version   = IFB_VERSION;
#if defined MSF_COMPONENT_ID //a new IFB demonstrates how dirty the solution is
	xxxx[xxxx_PRI_IDENTITY_OFFSET] = NULL;      //IFB_PRIIdentity placeholder   0xFD02
	xxxx[xxxx_PRI_IDENTITY_OFFSET+1] = NULL;    //IFB_PRISup placeholder        0xFD03
#endif // MSF_COMPONENT_ID
#if (HCF_TALLIES) & ( HCF_TALLIES_NIC | HCF_TALLIES_HCF )
	ifbp->IFB_TallyLen = 1 + 2 * (HCF_NIC_TAL_CNT + HCF_HCF_TAL_CNT);   //convert # of Tallies to L value for LTV
	ifbp->IFB_TallyTyp = CFG_TALLIES;           //IFB_TallyTyp: set T value
#endif // HCF_TALLIES_NIC / HCF_TALLIES_HCF
	ifbp->IFB_IOBase    = io_addr;              //set IO_Base asap, so asserts via HREG_SW_2 don't harm
	ifbp->IFB_IORange   = HREG_IO_RANGE;
	ifbp->IFB_CntlOpt   = USE_16BIT;
#if HCF_ASSERT
	assert_ifbp = ifbp;
	ifbp->IFB_AssertLvl = 1;
#if (HCF_ASSERT) & HCF_ASSERT_LNK_MSF_RTN
	if ( io_base != HCF_DISCONNECT ) {
		ifbp->IFB_AssertRtn = (MSF_ASSERT_RTNP)msf_assert;                                          /* 6 */
	}
#endif // HCF_ASSERT_LNK_MSF_RTN
#if (HCF_ASSERT) & HCF_ASSERT_MB                //build the structure to pass the assert info to hcf_put_info
	ifbp->IFB_AssertStrct.len = sizeof(ifbp->IFB_AssertStrct)/sizeof(hcf_16) - 1;
	ifbp->IFB_AssertStrct.typ = CFG_MB_INFO;
	ifbp->IFB_AssertStrct.base_typ = CFG_MB_ASSERT;
	ifbp->IFB_AssertStrct.frag_cnt = 1;
	ifbp->IFB_AssertStrct.frag_buf[0].frag_len =
		( offsetof(IFB_STRCT, IFB_AssertLvl) - offsetof(IFB_STRCT, IFB_AssertLine) ) / sizeof(hcf_16);
	ifbp->IFB_AssertStrct.frag_buf[0].frag_addr = &ifbp->IFB_AssertLine;
#endif // HCF_ASSERT_MB
#endif // HCF_ASSERT
	IF_PROT_TIME( prot_cnt = ifbp->IFB_TickIni = INI_TICK_INI );
#if ( (HCF_TYPE) & HCF_TYPE_PRELOADED ) == 0
	//!! No asserts before Reset-bit in HREG_IO is cleared
	OPW( HREG_IO, 0x0000 );                     //OPW useable                                       /* 2b*/
	HCF_WAIT_WHILE( (IPW( HREG_EV_STAT) & HREG_EV_CMD) == 0 );
	IF_PROT_TIME( HCFASSERT( prot_cnt, IPW( HREG_EV_STAT) ) );
	IF_PROT_TIME( if ( prot_cnt ) prot_cnt = ifbp->IFB_TickIni );
#endif // HCF_TYPE_PRELOADED
	//!! No asserts before Reset-bit in HREG_IO is cleared
	HCFASSERT( DO_ASSERT, MERGE_2( HCF_ASSERT, 0xCAF0 ) ); //just to proof that the complete assert machinery is working
	HCFASSERT( xa != CFG_FW_IDENTITY, 0 );       // assert if hcf_connect is called without intervening hcf_disconnect.
	HCFASSERT( ((hcf_32)(void*)ifbp & (HCF_ALIGN-1) ) == 0, (hcf_32)(void*)ifbp );
	HCFASSERT( (io_addr & 0x003F) == 0, io_addr );
	                                        //if Busy bit in Cmd register
	if (IPW( HREG_CMD ) & HCMD_BUSY ) {                                                             /* 8 */
		//.  Ack all to unblock a (possibly) blocked cmd pipe line
		OPW( HREG_EV_ACK, ~HREG_EV_SLEEP_REQ );
		                                //.  Wait for Busy bit drop  in Cmd register
		                                //.  Wait for Cmd  bit raise in Ev  register
		HCF_WAIT_WHILE( ( IPW( HREG_CMD ) & HCMD_BUSY ) && (IPW( HREG_EV_STAT) & HREG_EV_CMD) == 0 );
		IF_PROT_TIME( HCFASSERT( prot_cnt, IPW( HREG_EV_STAT) ) ); /* if prot_cnt == 0, cmd_exe will fail, causing DEFUNCT */
	}
	OPW( HREG_EV_ACK, ~HREG_EV_SLEEP_REQ );
#if ( (HCF_TYPE) & HCF_TYPE_PRELOADED ) == 0                                                        /*12*/
	(void)cmd_exe( ifbp, HCMD_INI, 0 );
#endif // HCF_TYPE_PRELOADED
	if ( io_base != HCF_DISCONNECT ) {
		rc = init( ifbp );                                                                          /*14*/
		if ( rc == HCF_SUCCESS ) {
			x.len = 2;
			x.typ = CFG_NIC_BUS_TYPE;
			(void)hcf_get_info( ifbp, &x );
			ifbp->IFB_BusType = x.val[0];
			//CFG_NIC_BUS_TYPE not supported -> default 32 bits/DMA, MSF has to overrule via CFG_CNTL_OPT
			if ( x.len == 0 || x.val[0] == 0x0002 || x.val[0] == 0x0003 ) {
#if (HCF_IO) & HCF_IO_32BITS
				ifbp->IFB_CntlOpt &= ~USE_16BIT;            //reset USE_16BIT
#endif // HCF_IO_32BITS
#if HCF_DMA
				ifbp->IFB_CntlOpt |= USE_DMA;               //SET DMA
#else
				ifbp->IFB_IORange = 0x40 /*i.s.o. HREG_IO_RANGE*/;
#endif // HCF_DMA
			}
		}
	} else HCFASSERT(  ( ifbp->IFB_Magic ^= HCF_MAGIC ) == 0, ifbp->IFB_Magic ) /*NOP*/;
	/* of above HCFASSERT only the side effect is needed, NOP in case HCFASSERT is dummy */
	ifbp->IFB_IOBase = io_base;                                                                     /* 0*/
	return rc;
} // hcf_connect

#if HCF_DMA
/************************************************************************************************************
 * Function get_frame_lst
 *  - resolve the "last host-owned descriptor" problems when a descriptor list is reclaimed by the MSF.
 *
 * The FrameList to be reclaimed as well as the DescriptorList always start in IFB_FirstDesc[tx_rx_flag]
 * and this is always the "current" DELWA Descriptor.
 *
 * If a FrameList is available, the last descriptor of the FrameList to turned into a new DELWA Descriptor:
 *  - a copy is made from the information in the last descriptor of the FrameList into the current
 *    DELWA Descriptor
 *  - the remainder of the DescriptorList is detached from the copy by setting the next_desc_addr at NULL
 *  - the DMA control bits of the copy are cleared to do not confuse the MSF
 *  - the copy of the last descriptor (i.e. the "old" DELWA Descriptor) is chained to the prev Descriptor
 *    of the FrameList, thus replacing the original last Descriptor of the FrameList.
 *  - IFB_FirstDesc is changed to the address of that replaced (original) last descriptor of the FrameList,
 *    i.e. the "new" DELWA Descriptor.
 *
 * This function makes a copy of that last host-owned descriptor, so the MSF will get a copy of the descriptor.
 * On top of that, it adjusts DMA related fields in the IFB structure.
 // perform a copying-scheme to circumvent the 'last host owned descriptor cannot be reclaimed' limitation imposed by H2.5's DMA hardware design
 // a 'reclaim descriptor' should be available in the HCF:
 *
 * Returns: address of the first descriptor of the FrameList
 *
 8: Be careful once you start re-ordering the steps in the copy process, that it still works for cases
 *   of FrameLists of 1, 2 and more than 2 descriptors
 *
 * Input parameters:
 * tx_rx_flag      : specifies 'transmit' or 'receive' descriptor.
 *
 ************************************************************************************************************/
HCF_STATIC DESC_STRCT*
get_frame_lst( IFBP ifbp, int tx_rx_flag )
{

	DESC_STRCT *head = ifbp->IFB_FirstDesc[tx_rx_flag];
	DESC_STRCT *copy, *p, *prev;

	HCFASSERT( tx_rx_flag == DMA_RX || tx_rx_flag == DMA_TX, tx_rx_flag );
	                                        //if FrameList
	if ( head ) {
		                                //.  search for last descriptor of first FrameList
		p = prev = head;
		while ( ( p->BUF_SIZE & DESC_EOP ) == 0 && p->next_desc_addr ) {
			if ( ( ifbp->IFB_CntlOpt & DMA_ENABLED ) == 0 ) {   //clear control bits when disabled
				p->BUF_CNT &= DESC_CNT_MASK;
			}
			prev = p;
			p = p->next_desc_addr;
		}
		                                //.  if DMA enabled
		if ( ifbp->IFB_CntlOpt & DMA_ENABLED ) {
			                        //.  .  if last descriptor of FrameList is DMA owned
			                        //.  .  or if FrameList is single (DELWA) Descriptor
			if ( p->BUF_CNT & DESC_DMA_OWNED || head->next_desc_addr == NULL ) {
				                //.  .  .  refuse to return FrameList to caller
				head = NULL;
			}
		}
	}
	                                        //if returnable FrameList found
	if ( head ) {
		                                //.  if FrameList is single (DELWA) Descriptor (implies DMA disabled)
		if ( head->next_desc_addr == NULL ) {
			                        //.  .  clear DescriptorList
			/*;?ifbp->IFB_LastDesc[tx_rx_flag] =*/ ifbp->IFB_FirstDesc[tx_rx_flag] = NULL;
			                        //.  else
		} else {
			                        //.  .  strip hardware-related bits from last descriptor
			                        //.  .  remove DELWA Descriptor from head of DescriptorList
			copy = head;
			head = head->next_desc_addr;
			                        //.   .  exchange first (Confined) and last (possibly imprisoned) Descriptor
			copy->buf_phys_addr = p->buf_phys_addr;
			copy->buf_addr = p->buf_addr;
			copy->BUF_SIZE = p->BUF_SIZE &= DESC_CNT_MASK;  //get rid of DESC_EOP and possibly DESC_SOP
			copy->BUF_CNT = p->BUF_CNT &= DESC_CNT_MASK;    //get rid of DESC_DMA_OWNED
#if (HCF_EXT) & HCF_DESC_STRCT_EXT
			copy->DESC_MSFSup = p->DESC_MSFSup;
#endif // HCF_DESC_STRCT_EXT
			                        //.  .  turn into a DELWA Descriptor
			p->buf_addr = NULL;
			                        //.  .  chain copy to prev                                          /* 8*/
			prev->next_desc_addr = copy;
			                        //.  .  detach remainder of the DescriptorList from FrameList
			copy->next_desc_addr = NULL;
			copy->next_desc_phys_addr = 0xDEAD0000; //! just to be nice, not really needed
			                        //.  .  save the new start (i.e. DELWA Descriptor) in IFB_FirstDesc
			ifbp->IFB_FirstDesc[tx_rx_flag] = p;
		}
		                                //.  strip DESC_SOP from first descriptor
		head->BUF_SIZE &= DESC_CNT_MASK;
		//head->BUF_CNT &= DESC_CNT_MASK;  get rid of DESC_DMA_OWNED
		head->next_desc_phys_addr = 0xDEAD0000; //! just to be nice, not really needed
	}
	                                        //return the just detached FrameList (if any)
	return head;
} // get_frame_lst


/************************************************************************************************************
 * Function put_frame_lst
 *
 * This function
 *
 * Returns: address of the first descriptor of the FrameList
 *
 * Input parameters:
 * tx_rx_flag      : specifies 'transmit' or 'receive' descriptor.
 *
 * The following list should be kept in sync with hcf_dma_tx/rx_put, in order to get them in the WCI-spec !!!!
 * Assert fails if
 * - DMA is not enabled
 * - descriptor list is NULL
 * - a descriptor in the descriptor list is not double word aligned
 * - a count of size field of a descriptor contains control bits, i.e. bits in the high order nibble.
 * - the DELWA descriptor is not a "singleton" DescriptorList.
 * - the DELWA descriptor is not the first Descriptor supplied
 * - a non_DMA descriptor is supplied before the DELWA Descriptor is supplied
 * - Possibly more checks could be added !!!!!!!!!!!!!

 *.NOTICE
 * The asserts marked with *sc* are really sanity checks for the HCF, they can (supposedly) not be influenced
 * by incorrect MSF behavior

 // The MSF is required to supply the HCF with a single descriptor for MSF tx reclaim purposes.
 // This 'reclaim descriptor' can be recognized by the fact that its buf_addr field is zero.
 *********************************************************************************************
 * Although not required from a hardware perspective:
 * - make each descriptor in this rx-chain DMA-owned.
 * - Also set the count to zero. EOP and SOP bits are also cleared.
 *********************************************************************************************/
HCF_STATIC void
put_frame_lst( IFBP ifbp, DESC_STRCT *descp, int tx_rx_flag )
{
	DESC_STRCT  *p = descp;
	hcf_16 port;

	HCFASSERT( ifbp->IFB_CntlOpt & USE_DMA, ifbp->IFB_CntlOpt); //only hcf_dma_tx_put must also be DMA_ENABLED
	HCFASSERT( tx_rx_flag == DMA_RX || tx_rx_flag == DMA_TX, tx_rx_flag );
	HCFASSERT( p , 0 );

	while ( p ) {
		HCFASSERT( ((hcf_32)p & 3 ) == 0, (hcf_32)p );
		HCFASSERT( (p->BUF_CNT & ~DESC_CNT_MASK) == 0, p->BUF_CNT );
		HCFASSERT( (p->BUF_SIZE & ~DESC_CNT_MASK) == 0, p->BUF_SIZE );
		p->BUF_SIZE &= DESC_CNT_MASK;                   //!!this SHOULD be superfluous in case of correct MSF
		p->BUF_CNT &= tx_rx_flag == DMA_RX ? 0 : DESC_CNT_MASK; //!!this SHOULD be superfluous in case of correct MSF
		p->BUF_CNT |= DESC_DMA_OWNED;
		if ( p->next_desc_addr ) {
//			HCFASSERT( p->buf_addr && p->buf_phys_addr  && p->BUF_SIZE && +/- p->BUF_SIZE, ... );
			HCFASSERT( p->next_desc_addr->desc_phys_addr, (hcf_32)p->next_desc_addr );
			p->next_desc_phys_addr = p->next_desc_addr->desc_phys_addr;
		} else {                                    //
			p->next_desc_phys_addr = 0;
			if ( p->buf_addr == NULL ) {            // DELWA Descriptor
				HCFASSERT( descp == p, (hcf_32)descp );  //singleton DescriptorList
				HCFASSERT( ifbp->IFB_FirstDesc[tx_rx_flag] == NULL, (hcf_32)ifbp->IFB_FirstDesc[tx_rx_flag]);
				HCFASSERT( ifbp->IFB_LastDesc[tx_rx_flag] == NULL, (hcf_32)ifbp->IFB_LastDesc[tx_rx_flag]);
				descp->BUF_CNT = 0; //&= ~DESC_DMA_OWNED;
				ifbp->IFB_FirstDesc[tx_rx_flag] = descp;
// part of alternative ifbp->IFB_LastDesc[tx_rx_flag] = ifbp->IFB_FirstDesc[tx_rx_flag] = descp;
				                // if "recycling" a FrameList
				                // (e.g. called from hcf_cntl( HCF_CNTL_ENABLE )
				                // .  prepare for activation DMA controller
// part of alternative descp = descp->next_desc_addr;
			} else {                                //a "real" FrameList, hand it over to the DMA engine
				HCFASSERT( ifbp->IFB_FirstDesc[tx_rx_flag], (hcf_32)descp );
				HCFASSERT( ifbp->IFB_LastDesc[tx_rx_flag], (hcf_32)descp );
				HCFASSERT( ifbp->IFB_LastDesc[tx_rx_flag]->next_desc_addr == NULL,
					   (hcf_32)ifbp->IFB_LastDesc[tx_rx_flag]->next_desc_addr);
//				p->buf_cntl.cntl_stat |= DESC_DMA_OWNED;
				ifbp->IFB_LastDesc[tx_rx_flag]->next_desc_addr = descp;
				ifbp->IFB_LastDesc[tx_rx_flag]->next_desc_phys_addr = descp->desc_phys_addr;
				port = HREG_RXDMA_PTR32;
				if ( tx_rx_flag ) {
					p->BUF_SIZE |= DESC_EOP;    // p points at the last descriptor in the caller-supplied descriptor chain
					descp->BUF_SIZE |= DESC_SOP;
					port = HREG_TXDMA_PTR32;
				}
				OUT_PORT_DWORD( (ifbp->IFB_IOBase + port), descp->desc_phys_addr );
			}
			ifbp->IFB_LastDesc[tx_rx_flag] = p;
		}
		p = p->next_desc_addr;
	}
} // put_frame_lst


/************************************************************************************************************
 *
 *.MODULE        DESC_STRCT* hcf_dma_rx_get( IFBP ifbp )
 *.PURPOSE       decapsulate a message and provides that message to the MSF.
 *               reclaim all descriptors in the rx descriptor chain.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS
 *   pointer to a FrameList
 *
 *.DESCRIPTION
 * hcf_dma_rx_get is intended to  return a received frame when such a frame is deposited in Host memory by the
 * DMA engine. In addition hcf_dma_rx_get can be used to reclaim all descriptors in the rx descriptor chain
 * when the DMA Engine is disabled, e.g. as part of a driver unloading strategy.
 * hcf_dma_rx_get must be called repeatedly by the MSF when hcf_service_nic signals availability of a rx frame
 * through the HREG_EV_RDMAD flag of IFB_DmaPackets. The calling must stop when a NULL pointer is returned, at
 * which time the HREG_EV_RDMAD flag is also cleared by the HCF to arm the mechanism for the next frame
 * reception.
 * Regardless whether the DMA Engine is currently enabled (as controlled via hcf_cntl), if the DMA controller
 * deposited an Rx-frame in the Rx-DescriptorList, this frame is detached from the Rx-DescriptorList,
 * transformed into a FrameList (i.e.  updating the housekeeping fields in the descriptors) and returned to the
 * caller.
 * If no such Rx-frame is available in the Rx-DescriptorList, the behavior of hcf_dma_rx_get depends on the
 * status of the DMA Engine.
 * If the DMA Engine is enabled, a NULL pointer is returned.
 * If the DMA Engine is disabled, the following strategy is used:
 * - the complete Rx-DescriptorList is returned. The DELWA Descriptor is not part of the Rx-DescriptorList.
 * - If there is no Rx-DescriptorList, the DELWA Descriptor is returned.
 * - If there is no DELWA Descriptor, a NULL pointer is returned.
 *
 * If the MSF performs an disable/enable sequence without exhausting the Rx-DescriptorList as described above,
 * the enable command will reset all house keeping information, i.e. already received but not yet by the MSF
 * retrieved frames are lost and the next frame will be received starting with the oldest descriptor.
 *
 * The HCF can be used in 2 fashions: with and without decapsulation for data transfer.
 * This is controlled at compile time by the HCF_ENC bit of the HCF_ENCAP system constant.
 * If appropriate, decapsulation is done by moving some data inside the buffers and updating the descriptors
 * accordingly.
 *!! ;?????where did I describe why a simple manipulation with the count values does not suffice?
 *
 *.DIAGRAM
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/

DESC_STRCT*
hcf_dma_rx_get (IFBP ifbp)
{
	DESC_STRCT *descp;  // pointer to start of FrameList

	descp = get_frame_lst( ifbp, DMA_RX );
	if ( descp && descp->buf_addr ) {

		                                //skip decapsulation at confined descriptor
#if (HCF_ENCAP) == HCF_ENC
		int i;
		DESC_STRCT *p = descp->next_desc_addr;  //pointer to 2nd descriptor of frame
		HCFASSERT(p, 0);
		// The 2nd descriptor contains (maybe) a SNAP header plus part or whole of the payload.
		//determine decapsulation sub-flag in RxFS
		i = *(wci_recordp)&descp->buf_addr[HFS_STAT] & ( HFS_STAT_MSG_TYPE | HFS_STAT_ERR );
		if ( i == HFS_STAT_TUNNEL ||
		     ( i == HFS_STAT_1042 && hcf_encap( (wci_bufp)&p->buf_addr[HCF_DASA_SIZE] ) != ENC_TUNNEL )) {
			// The 2nd descriptor contains a SNAP header plus part or whole of the payload.
			HCFASSERT( p->BUF_CNT == (p->buf_addr[5] + (p->buf_addr[4]<<8) + 2*6 + 2 - 8), p->BUF_CNT );
			// perform decapsulation
			HCFASSERT(p->BUF_SIZE >=8, p->BUF_SIZE);
			// move SA[2:5] in the second buffer to replace part of the SNAP header
			for ( i=3; i >= 0; i--) p->buf_addr[i+8] = p->buf_addr[i];
			// copy DA[0:5], SA[0:1] from first buffer to second buffer
			for ( i=0; i<8; i++) p->buf_addr[i] = descp->buf_addr[HFS_ADDR_DEST + i];
			// make first buffer shorter in count
			descp->BUF_CNT = HFS_ADDR_DEST;
		}
	}
#endif // HCF_ENC
	if ( descp == NULL ) ifbp->IFB_DmaPackets &= (hcf_16)~HREG_EV_RDMAD;  //;?could be integrated into get_frame_lst
	HCFLOGEXIT( HCF_TRACE_DMA_RX_GET );
	return descp;
} // hcf_dma_rx_get


/************************************************************************************************************
 *
 *.MODULE        void hcf_dma_rx_put( IFBP ifbp, DESC_STRCT *descp )
 *.PURPOSE       supply buffers for receive purposes.
 *               supply the Rx-DELWA descriptor.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   descp       address of a DescriptorList
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * This function is called by the MSF to supply the HCF with new/more buffers for receive purposes.
 * The HCF can be used in 2 fashions: with and without encapsulation for data transfer.
 * This is controlled at compile time by the HCF_ENC bit of the HCF_ENCAP system constant.
 * As a consequence, some additional constraints apply to the number of descriptor and the buffers associated
 * with the first 2 descriptors. Independent of the encapsulation feature, the COUNT fields are ignored.
 * A special case is the supplying of the DELWA descriptor, which must be supplied as the first descriptor.
 *
 * Assert fails if
 * - ifbp has a recognizable out-of-range value.
 * - NIC interrupts are not disabled while required by parameter action.
 * - in case decapsulation by the HCF is selected:
 *     - The first databuffer does not have the exact size corresponding with the RxFS up to the 802.3 DestAddr
 *       field (== 29 words).
 *     - The FrameList does not consists of at least 2 Descriptors.
 *     - The second databuffer does not have the minimum size of 8 bytes.
 *!! The 2nd part of the list of asserts should be kept in sync with put_frame_lst, in order to get
 *!! them in the WCI-spec !!!!
 * - DMA is not enabled
 * - descriptor list is NULL
 * - a descriptor in the descriptor list is not double word aligned
 * - a count of size field of a descriptor contains control bits, i.e. bits in the high order nibble.
 * - the DELWA descriptor is not a "singleton" DescriptorList.
 * - the DELWA descriptor is not the first Descriptor supplied
 * - a non_DMA descriptor is supplied before the DELWA Descriptor is supplied
 *!! - Possibly more checks could be added !!!!!!!!!!!!!
 *
 *.DIAGRAM
 *
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
void
hcf_dma_rx_put( IFBP ifbp, DESC_STRCT *descp )
{

	HCFLOGENTRY( HCF_TRACE_DMA_RX_PUT, 0xDA01 );
	HCFASSERT( ifbp->IFB_Magic == HCF_MAGIC, ifbp->IFB_Magic );
	HCFASSERT_INT;

	put_frame_lst( ifbp, descp, DMA_RX );
#if HCF_ASSERT && (HCF_ENCAP) == HCF_ENC
	if ( descp->buf_addr ) {
		HCFASSERT( descp->BUF_SIZE == HCF_DMA_RX_BUF1_SIZE, descp->BUF_SIZE );
		HCFASSERT( descp->next_desc_addr, 0 ); // first descriptor should be followed by another descriptor
		// The second DB is for SNAP and payload purposes. It should be a minimum of 12 bytes in size.
		HCFASSERT( descp->next_desc_addr->BUF_SIZE >= 12, descp->next_desc_addr->BUF_SIZE );
	}
#endif // HCFASSERT / HCF_ENC
	HCFLOGEXIT( HCF_TRACE_DMA_RX_PUT );
} // hcf_dma_rx_put


/************************************************************************************************************
 *
 *.MODULE        DESC_STRCT* hcf_dma_tx_get( IFBP ifbp )
 *.PURPOSE       DMA mode: reclaims and decapsulates packets in the tx descriptor chain if:
 *                - A Tx packet has been copied from host-RAM into NIC-RAM by the DMA engine
 *                - The Hermes/DMAengine have been disabled
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS
 *   pointer to a reclaimed Tx packet.
 *
 *.DESCRIPTION
 * impact of the disable command:
 * When a non-empty pool of Tx descriptors exists (created by means of hcf_dma_put_tx), the MSF
 * is supposed to empty that pool by means of hcf_dma_tx_get calls after the disable in an
 * disable/enable sequence.
 *
 *.DIAGRAM
 *
 *.NOTICE
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
DESC_STRCT*
hcf_dma_tx_get( IFBP ifbp )
{
	DESC_STRCT *descp;  // pointer to start of FrameList

	descp = get_frame_lst( ifbp, DMA_TX );
	if ( descp && descp->buf_addr ) {
		                                //skip decapsulation at confined descriptor
#if (HCF_ENCAP) == HCF_ENC
		if ( ( descp->BUF_CNT == HFS_TYPE )) {
			// perform decapsulation if needed
			descp->next_desc_addr->buf_phys_addr -= HCF_DASA_SIZE;
			descp->next_desc_addr->BUF_CNT       += HCF_DASA_SIZE;
		}
#endif // HCF_ENC
	}
	if ( descp == NULL ) {  //;?could be integrated into get_frame_lst
		ifbp->IFB_DmaPackets &= (hcf_16)~HREG_EV_TDMAD;
	}
	HCFLOGEXIT( HCF_TRACE_DMA_TX_GET );
	return descp;
} // hcf_dma_tx_get


/************************************************************************************************************
 *
 *.MODULE        void hcf_dma_tx_put( IFBP ifbp, DESC_STRCT *descp, hcf_16 tx_cntl )
 *.PURPOSE       puts a packet in the Tx DMA queue in host ram and kicks off the TxDma engine.
 *               supply the Tx-DELWA descriptor.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   descp       address of Tx Descriptor Chain (i.e. a single Tx frame)
 *   tx_cntl     indicates MAC-port and (Hermes) options
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * The HCF can be used in 2 fashions: with and without encapsulation for data transfer.
 * This is controlled at compile time by the HCF_ENC bit of the HCF_ENCAP system constant.
 *
 * Regardless of the HCF_ENCAP system constant, the descriptor list created to describe the frame to be
 * transmitted, must supply space to contain the 802.11 header, preceding the actual frame to be transmitted.
 * Basically, this only supplies working storage to the HCF which passes this on to the DMA engine.
 * As a consequence the contents of this space do not matter.
 * Nevertheless BUF_CNT must take in account this storage.
 * This working space to contain the 802.11 header may not be fragmented, the first buffer must be
 * sufficiently large to contain at least the 802.11 header, i.e. HFS_ADDR_DEST (29 words or 0x3A bytes).
 * This way, the HCF can simply, regardless whether or not the HCF encapsulates the frame, write the parameter
 * tx_cntl at offset 0x36 (HFS_TX_CNTL) in the first buffer.
 * Note that it is allowed to have part or all of the actual frame represented by the first descriptor as long
 * as the requirement for storage for the 802.11 header is met, i.e. the 802.3 frame starts at offset
 * HFS_ADDR_DEST.
 * Except for the Assert on the 1st buffer in case of Encapsualtion, the SIZE fields are ignored.
 *
 * In case the encapsulation feature is compiled in, there are the following additional requirements.
 * o The BUF_CNT of the first buffer changes from a minimum of 0x3A bytes to exactly 0x3A, i.e. the workspace
 *   to store the 802.11 header
 * o The BUF_SIZE of the first buffer is at least the space needed to store the
 *   - 802.11 header (29 words)
 *   - 802.3 header, i.e. 12 bytes addressing information and 2 bytes length field
 *   - 6 bytes SNAP-header
 *   This results in 39 words or 0x4E bytes or HFS_TYPE.
 *   Note that if the BUF_SIZE is larger than 0x4E, this surplus is not used.
 * o The actual frame begins in the 2nd descriptor (which is already implied by the BUF_CNT == 0x3A requirement) and the associated buffer contains at least the 802.3 header, i.e. the 14 bytes representing addressing information and length/type field
 *
 *   When the HCF does not encapsulates (i.e. length/type field <= 1500),  no changes are made to descriptors
 *   or buffers.
 *
 *   When the HCF actually encapsulates (i.e. length/type field > 1500), it successively writes, starting at
 *   offset HFS_ADDR_DEST (0x3A) in the first buffer:
 *     - the 802.3 addressing information, copied from the begin of the second buffer
 *     - the frame length, derived from the total length of the individual fragments, corrected for the SNAP
 *       header length and Type field and ignoring the Destination Address, Source Address and Length field
 *     - the appropriate snap header (Tunnel or 1042, depending on the value of the type field).
 *
 *    The information in the first two descriptors is adjusted accordingly:
 *     - the first descriptor count is changed from 0x3A to 0x4E (HFS_TYPE), which matches 0x3A + 12 + 2 + 6
 *     - the second descriptor count is decreased by 12, being the moved addressing information
 *     - the second descriptor (physical) buffer address is increased by 12.
 *
 * When the descriptors are returned by hcf_dma_tx_get, the transformation of the first two descriptors is
 * undone.
 *
 * Under any of the above scenarios, the assert BUF_CNT <= BUF_SIZE must be true for all descriptors
 * In case of encapsulation, BUF_SIZE of the 1st descriptor is asserted to be at least HFS_TYPE (0x4E), so it is NOT tested.
 *
 * Assert fails if
 * - ifbp has a recognizable out-of-range value.
 * - tx_cntl has a recognizable out-of-range value.
 * - NIC interrupts are not disabled while required by parameter action.
 * - in case encapsulation by the HCF is selected:
 *     - The FrameList does not consists of at least 2 Descriptors.
 *     - The first databuffer does not contain exactly the (space for) the 802.11 header (== 28 words)
 *     - The first databuffer does not have a size to additionally accommodate the 802.3 header and the
 *       SNAP header of the frame after encapsulation (== 39 words).
 *     - The second databuffer does not contain at least DA, SA and 'type/length' (==14 bytes or 7 words)
 *!! The 2nd part of the list of asserts should be kept in sync with put_frame_lst, in order to get
 *!! them in the WCI-spec !!!!
 * - DMA is not enabled
 * - descriptor list is NULL
 * - a descriptor in the descriptor list is not double word aligned
 * - a count of size field of a descriptor contains control bits, i.e. bits in the high order nibble.
 * - the DELWA descriptor is not a "singleton" DescriptorList.
 * - the DELWA descriptor is not the first Descriptor supplied
 * - a non_DMA descriptor is supplied before the DELWA Descriptor is supplied
 *!! - Possibly more checks could be added !!!!!!!!!!!!!
 *.DIAGRAM
 *
 *.NOTICE
 *
 *.ENDDOC                END DOCUMENTATION
 *
 *
 *1: Write tx_cntl parameter to HFS_TX_CNTL field into the Hermes-specific header in buffer 1
 *4: determine whether encapsulation is needed and write the type (tunnel or 1042) already at the appropriate
 *   offset in the 1st buffer
 *6: Build the encapsualtion enveloppe in the free space at the end of the 1st buffer
 *   - Copy DA/SA fields from the 2nd buffer
 *   - Calculate total length of the message (snap-header + type-field + the length of all buffer fragments
 *     associated with the 802.3 frame (i.e all descriptors except the first), but not the DestinationAddress,
 *     SourceAddress and length-field)
 *     Assert the message length
 *     Write length. Note that the message is in BE format, hence on LE platforms the length must be converted
 *     ;? THIS IS NOT WHAT CURRENTLY IS IMPLEMENTED
 *   - Write snap header. Note that the last byte of the snap header is NOT copied, that byte is already in
 *     place as result of the call to hcf_encap.
 *   Note that there are many ways to skin a cat. To express the offsets in the 1st buffer while writing
 *   the snap header, HFS_TYPE is chosen as a reference point to make it easier to grasp that the snap header
 *   and encapsualtion type are at least relative in the right.
 *8: modify 1st descriptor to reflect moved part of the 802.3 header + Snap-header
 *   modify 2nd descriptor to skip the moved part of the 802.3 header (DA/SA
 *10: set each descriptor to 'DMA owned',  clear all other control bits.
 *   Set SOP bit on first descriptor. Set EOP bit on last descriptor.
 *12: Either append the current frame to an existing descriptor list or
 *14: create a list beginning with the current frame
 *16: remember the new end of the list
 *20: hand the frame over to the DMA engine
 ************************************************************************************************************/
void
hcf_dma_tx_put( IFBP ifbp, DESC_STRCT *descp, hcf_16 tx_cntl )
{
	DESC_STRCT  *p = descp->next_desc_addr;
	int         i;

#if HCF_ASSERT
	int x = ifbp->IFB_FWIdentity.comp_id == COMP_ID_FW_AP ? tx_cntl & ~HFS_TX_CNTL_PORT : tx_cntl;
	HCFASSERT( (x & ~HCF_TX_CNTL_MASK ) == 0, tx_cntl );
#endif // HCF_ASSERT
	HCFLOGENTRY( HCF_TRACE_DMA_TX_PUT, 0xDA03 );
	HCFASSERT( ifbp->IFB_Magic == HCF_MAGIC, ifbp->IFB_Magic );
	HCFASSERT_INT;
	HCFASSERT( ( ifbp->IFB_CntlOpt & (USE_DMA|DMA_ENABLED) ) == (USE_DMA|DMA_ENABLED), ifbp->IFB_CntlOpt);

	if ( descp->buf_addr ) {
		*(hcf_16*)(descp->buf_addr + HFS_TX_CNTL) = tx_cntl;                                            /*1*/
#if (HCF_ENCAP) == HCF_ENC
		HCFASSERT( descp->next_desc_addr, 0 );                                   //at least 2 descripors
		HCFASSERT( descp->BUF_CNT == HFS_ADDR_DEST, descp->BUF_CNT );    //exact length required for 1st buffer
		HCFASSERT( descp->BUF_SIZE >= HCF_DMA_TX_BUF1_SIZE, descp->BUF_SIZE );   //minimal storage for encapsulation
		HCFASSERT( p->BUF_CNT >= 14, p->BUF_CNT );                  //at least DA, SA and 'type' in 2nd buffer

		descp->buf_addr[HFS_TYPE-1] = hcf_encap(&descp->next_desc_addr->buf_addr[HCF_DASA_SIZE]);       /*4*/
		if ( descp->buf_addr[HFS_TYPE-1] != ENC_NONE ) {
			for ( i=0; i < HCF_DASA_SIZE; i++ ) {                                                       /*6*/
				descp->buf_addr[i + HFS_ADDR_DEST] = descp->next_desc_addr->buf_addr[i];
			}
			i = sizeof(snap_header) + 2 - ( 2*6 + 2 );
			do { i += p->BUF_CNT; } while ( ( p = p->next_desc_addr ) != NULL );
			*(hcf_16*)(&descp->buf_addr[HFS_LEN]) = CNV_END_SHORT(i);   //!! this converts on ALL platforms, how does that relate to the CCX code
			for ( i=0; i < sizeof(snap_header) - 1; i++) {
				descp->buf_addr[HFS_TYPE - sizeof(snap_header) + i] = snap_header[i];
			}
			descp->BUF_CNT = HFS_TYPE;                                                                  /*8*/
			descp->next_desc_addr->buf_phys_addr    += HCF_DASA_SIZE;
			descp->next_desc_addr->BUF_CNT          -= HCF_DASA_SIZE;
		}
#endif // HCF_ENC
	}
	put_frame_lst( ifbp, descp, DMA_TX );
	HCFLOGEXIT( HCF_TRACE_DMA_TX_PUT );
} // hcf_dma_tx_put

#endif // HCF_DMA

/************************************************************************************************************
 *
 *.MODULE        hcf_8 hcf_encap( wci_bufp type )
 *.PURPOSE       test whether RFC1042 or Bridge-Tunnel encapsulation is needed.
 *
 *.ARGUMENTS
 *   type        (Far) pointer to the (Big Endian) Type/Length field in the message
 *
 *.RETURNS
 *   ENC_NONE        len/type is "len" ( (BIG_ENDIAN)type <= 1500 )
 *   ENC_TUNNEL      len/type is "type" and 0x80F3 or 0x8137
 *   ENC_1042        len/type is "type" but not 0x80F3 or 0x8137
 *
 *.CONDITIONS
 *   NIC Interrupts  d.c
 *
 *.DESCRIPTION
 * Type must point to the Len/Type field of the message, this is the 2-byte field immediately after the 6 byte
 * Destination Address and 6 byte Source Address.  The 2 successive bytes addressed by type are interpreted as
 * a Big Endian value.  If that value is less than or equal to 1500, the message is assumed to be in 802.3
 * format.  Otherwise the message is assumed to be in Ethernet-II format.  Depending on the value of Len/Typ,
 * Bridge Tunnel or RFC1042 encapsulation is needed.
 *
 *.DIAGRAM
 *
 *  1:   presume 802.3, hence preset return value at ENC_NONE
 *  2:   convert type from "network" Endian format to native Endian
 *  4:   the litmus test to distinguish type and len.
 *   The hard code "magic" value of 1500 is intentional and should NOT be replaced by a mnemonic because it is
 *   not related at all to the maximum frame size supported  by the Hermes.
 *  6:   check type against:
 *       0x80F3  //AppleTalk Address Resolution Protocol (AARP)
 *       0x8137  //IPX
 *   to determine the type of encapsulation
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC hcf_8
hcf_encap( wci_bufp type )
{

	hcf_8   rc = ENC_NONE;                                                                                  /* 1 */
	hcf_16  t = (hcf_16)(*type<<8) + *(type+1);                                                             /* 2 */

	if ( t > 1500 ) {                                                                                   /* 4 */
		if ( t == 0x8137 || t == 0x80F3 ) {
			rc = ENC_TUNNEL;                                                                            /* 6 */
		} else {
			rc = ENC_1042;
		}
	}
	return rc;
} // hcf_encap


/************************************************************************************************************
 *
 *.MODULE        int hcf_get_info( IFBP ifbp, LTVP ltvp )
 *.PURPOSE       Obtains transient and persistent configuration information from the Card and from the HCF.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   ltvp        address of LengthTypeValue structure specifying the "what" and the "how much" of the
 *               information to be collected from the HCF or from the Hermes
 *
 *.RETURNS
 *   HCF_ERR_LEN         The provided buffer was too small
 *   HCF_SUCCESS         Success
 *!! via cmd_exe ( type >= CFG_RID_FW_MIN )
 *   HCF_ERR_NO_NIC      NIC removed during retrieval
 *   HCF_ERR_TIME_OUT    Expected Hermes event did not occur in expected time
 *!! via cmd_exe and setup_bap (type >= CFG_RID_FW_MIN )
 *   HCF_ERR_DEFUNCT_... HCF is in defunct mode (bits 0x7F reflect cause)
 *
 *.DESCRIPTION
 * The T-field of the LTV-record (provided by the MSF in parameter ltvp) specifies the RID wanted. The RID
 * information identified by the T-field is copied into the V-field.
 * On entry, the L-field specifies the size of the buffer, also called the "Initial DataLength". The L-value
 * includes the size of the T-field, but not the size of the L-field itself.
 * On return, the L-field indicates the number of words actually contained by the Type and Value fields.
 * As the size of the Type field in the LTV-record is included in the "Initial DataLength" of the record, the
 * V-field can contain at most "Initial DataLength" - 1 words of data.
 * Copying stops if either the complete Information is copied or if the number of words indicated by the
 * "Initial DataLength" were copied.  The "Initial DataLength" acts as a safe guard against Configuration
 * Information blocks that have different sizes for different F/W versions, e.g. when later versions support
 * more tallies than earlier versions.
 * If the size of Value field of the RID exceeds the size of the "Initial DataLength" -1, as much data
 * as fits is copied, and an error status of HCF_ERR_LEN is returned.
 *
 * It is the responsibility of the MSF to detect card removal and re-insertion and not call the HCF when the
 * NIC is absent. The MSF cannot, however, timely detect a Card removal if the Card is removed while
 * hcf_get_info is in progress.  Therefore, the HCF performs its own check on Card presence after the read
 * operation of the NIC data.  If the Card is not present or removed during the execution of hcf_get_info,
 * HCF_ERR_NO_NIC is returned and the content of the Data Buffer is unpredictable. This check is not performed
 * in case of the "HCF embedded" pseudo RIDs like CFG_TALLIES.
 *
 * Assert fails if
 * - ifbp has a recognizable out-of-range value.
 * - reentrancy, may be  caused by calling hcf_functions without adequate protection
 *   against NIC interrupts or multi-threading.
 * - ltvp is a NULL pointer.
 * - length field of the LTV-record at entry is 0 or 1 or has an excessive value (i.e. exceeds HCF_MAX_LTV).
 * - type field of the LTV-record is invalid.
 *
 *.DIAGRAM
 *   Hcf_get_mb_info copies the contents of the oldest MailBox Info block in the MailBox to PC RAM. If len is
 *   less than the size of the MailBox Info block, only as much as fits in the PC RAM buffer is copied. After
 *   the copying the MailBox Read pointer is updated to point to the next MailBox Info block, hence the
 *   remainder of an "oversized" MailBox Info block is lost. The truncation of the MailBox Info block is NOT
 *   reflected in the return status. Note that hcf_get_info guarantees the length of the PC RAM buffer meets
 *   the minimum requirements of at least 2, so no PC RAM buffer overrun.
 *
 *   Calling hcf_get_mb_info when their is no MailBox Info block available or when there is no MailBox at all,
 *   results in a "NULL" MailBox Info block.
 *
 *12:    see NOTICE
 *17: The return status of cmd_wait and the first hcfio_in_string can be ignored, because when one fails, the
 *   other fails via the IFB_DefunctStat mechanism
 *20: "HCFASSERT( rc == HCF_SUCCESS, rc )" is not suitable because this will always trigger as side effect of
 *   the HCFASSERT in hcf_put_info which calls hcf_get_info to figure out whether the RID exists at all.

 *.NOTICE
 *
 *   "HCF embedded" pseudo RIDs:
 *   CFG_MB_INFO, CFG_TALLIES, CFG_DRV_IDENTITY, CFG_DRV_SUP_RANGE, CFG_DRV_ACT_RANGES_PRI,
 *   CFG_DRV_ACT_RANGES_STA, CFG_DRV_ACT_RANGES_HSI
 *   Note the HCF_ERR_LEN is NOT adequately set, when L >= 2 but less than needed
 *
 *   Remarks: Transfers operation information and transient and persistent configuration information from the
 *   Card and from the HCF to the MSF.
 *   The exact layout of the provided data structure depends on the action code. Copying stops if either the
 *   complete Configuration Information is copied or if the number of bytes indicated by len is copied.  Len
 *   acts as a safe guard against Configuration Information blocks which have different sizes for different
 *   Hermes versions, e.g. when later versions support more tallies than earlier versions. It is a conscious
 *   decision that unused parts of the PC RAM buffer are not cleared.
 *
 *   Remarks: The only error against which is protected is the "Read error" as result of Card removal. Only the
 *   last hcf_io_string need to be protected because if the first fails the second will fail as well. Checking
 *   for cmd_exe errors is supposed superfluous because problems in cmd_exe are already caught or will be
 *   caught by hcf_enable.
 *
 *   CFG_MB_INFO: copy the oldest MailBox Info Block or the "null" block if none available.
 *
 *   The mechanism to HCF_ASSERT on invalid typ-codes in the LTV record is based on the following strategy:
 *     - during the pseudo-asynchronous Hermes commands (diagnose, download) only CFG_MB_INFO is acceptable
 *     - some codes (e.g. CFG_TALLIES) are explicitly handled by the HCF which implies that these codes
 *       are valid
 *     - all other codes in the range 0xFC00 through 0xFFFF are passed to the Hermes.  The Hermes returns an
 *       LTV record with a zero value in the L-field for all Typ-codes it does not recognize. This is
 *       defined and intended behavior, so HCF_ASSERT does not catch on this phenomena.
 *     - all remaining codes are invalid and cause an ASSERT.
 *
 *.CONDITIONS
 * In case of USB, HCF_MAX_MSG ;?USED;? to limit the amount of data that can be retrieved via hcf_get_info.
 *
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
int
hcf_get_info( IFBP ifbp, LTVP ltvp )
{

	int         rc = HCF_SUCCESS;
	hcf_16      len = ltvp->len;
	hcf_16      type = ltvp->typ;
	wci_recordp p = &ltvp->len;     //destination word pointer (in LTV record)
	hcf_16      *q = NULL;              /* source word pointer  Note!! DOS COM can't cope with FAR
					     * as a consequence MailBox must be near which is usually true anyway
					     */
	int         i;

	HCFLOGENTRY( HCF_TRACE_GET_INFO, ltvp->typ );
	HCFASSERT( ifbp->IFB_Magic == HCF_MAGIC, ifbp->IFB_Magic );
	HCFASSERT_INT;
	HCFASSERT( ltvp, 0 );
	HCFASSERT( 1 < ltvp->len && ltvp->len <= HCF_MAX_LTV + 1, MERGE_2( ltvp->typ, ltvp->len ) );

	ltvp->len = 0;                              //default to: No Info Available
	//filter out all specials
	for ( i = 0; ( q = xxxx[i] ) != NULL && q[1] != type; i++ ) /*NOP*/;

#if HCF_TALLIES
	if ( type == CFG_TALLIES ) {                                                    /*3*/
		(void)hcf_action( ifbp, HCF_ACT_TALLIES );
		q = (hcf_16*)&ifbp->IFB_TallyLen;
	}
#endif // HCF_TALLIES

	if ( type == CFG_MB_INFO ) {
		if ( ifbp->IFB_MBInfoLen ) {
			if ( ifbp->IFB_MBp[ifbp->IFB_MBRp] == 0xFFFF ) {
				ifbp->IFB_MBRp = 0; //;?Probably superfluous
			}
			q = &ifbp->IFB_MBp[ifbp->IFB_MBRp];
			ifbp->IFB_MBRp += *q + 1;   //update read pointer
			if ( ifbp->IFB_MBp[ifbp->IFB_MBRp] == 0xFFFF ) {
				ifbp->IFB_MBRp = 0;
			}
			ifbp->IFB_MBInfoLen = ifbp->IFB_MBp[ifbp->IFB_MBRp];
		}
	}

	if ( q != NULL ) {                      //a special or CFG_TALLIES or CFG_MB_INFO
		i = min( len, *q ) + 1;             //total size of destination (including T-field)
		while ( i-- ) {
			*p++ = *q;
#if (HCF_TALLIES) & HCF_TALLIES_RESET
			if ( q > &ifbp->IFB_TallyTyp && type == CFG_TALLIES ) {
				*q = 0;
			}
#endif // HCF_TALLIES_RESET
			q++;
		}
	} else {                                // not a special nor CFG_TALLIES nor CFG_MB_INFO
		if ( type == CFG_CNTL_OPT ) {                                       //read back effective options
			ltvp->len = 2;
			ltvp->val[0] = ifbp->IFB_CntlOpt;
#if (HCF_EXT) & HCF_EXT_NIC_ACCESS
		} else if ( type == CFG_PROD_DATA ) {  //only needed for some test tool on top of H-II NDIS driver
			hcf_io      io_port;
			wci_bufp    pt;                 //pointer with the "right" type, just to help ease writing macros with embedded assembly
			OPW( HREG_AUX_PAGE, (hcf_16)(PLUG_DATA_OFFSET >> 7) );
			OPW( HREG_AUX_OFFSET, (hcf_16)(PLUG_DATA_OFFSET & 0x7E) );
			io_port = ifbp->IFB_IOBase + HREG_AUX_DATA;     //to prevent side effects of the MSF-defined macro
			p = ltvp->val;                  //destination char pointer (in LTV record)
			i = len - 1;
			if (i > 0 ) {
				pt = (wci_bufp)p;   //just to help ease writing macros with embedded assembly
				IN_PORT_STRING_8_16( io_port, pt, i ); //space used by T: -1
			}
		} else if ( type == CFG_CMD_HCF ) {
#define P ((CFG_CMD_HCF_STRCT FAR *)ltvp)
			HCFASSERT( P->cmd == CFG_CMD_HCF_REG_ACCESS, P->cmd );       //only Hermes register access supported
			if ( P->cmd == CFG_CMD_HCF_REG_ACCESS ) {
				HCFASSERT( P->mode < ifbp->IFB_IOBase, P->mode );        //Check Register space
				ltvp->len = min( len, 4 );                              //RESTORE ltv length
				P->add_info = IPW( P->mode );
			}
#undef P
#endif // HCF_EXT_NIC_ACCESS
#if (HCF_ASSERT) & HCF_ASSERT_PRINTF
		} else if (type == CFG_FW_PRINTF) {
			rc = fw_printf(ifbp, (CFG_FW_PRINTF_STRCT*)ltvp);
#endif // HCF_ASSERT_PRINTF
		} else if ( type >= CFG_RID_FW_MIN ) {
//;? by using HCMD_BUSY option when calling cmd_exe, using a get_frag with length 0 just to set up the
//;? BAP and calling cmd_cmpl, you could merge the 2 Busy waits. Whether this really helps (and what
//;? would be the optimal sequence in cmd_exe and get_frag) would have to be MEASURED
		/*17*/  if ( ( rc = cmd_exe( ifbp, HCMD_ACCESS, type ) ) == HCF_SUCCESS &&
				 ( rc = setup_bap( ifbp, type, 0, IO_IN ) ) == HCF_SUCCESS ) {
				get_frag( ifbp, (wci_bufp)&ltvp->len, 2*len+2 BE_PAR(2) );
				if ( IPW( HREG_STAT ) == 0xFFFF ) {                 //NIC removal test
					ltvp->len = 0;
					HCFASSERT( DO_ASSERT, type );
				}
			}
	/*12*/  } else HCFASSERT( DO_ASSERT, type ) /*NOP*/; //NOP in case HCFASSERT is dummy
	}
	if ( len < ltvp->len ) {
		ltvp->len = len;
		if ( rc == HCF_SUCCESS ) {
			rc = HCF_ERR_LEN;
		}
	}
	HCFASSERT( rc == HCF_SUCCESS || ( rc == HCF_ERR_LEN && ifbp->IFB_AssertTrace & 1<<HCF_TRACE_PUT_INFO ),
		   MERGE_2( type, rc ) );                                                                /*20*/
	HCFLOGEXIT( HCF_TRACE_GET_INFO );
	return rc;
} // hcf_get_info


/************************************************************************************************************
 *
 *.MODULE        int hcf_put_info( IFBP ifbp, LTVP ltvp )
 *.PURPOSE       Transfers operation and configuration information to the Card and to the HCF.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   ltvp        specifies the RID (as defined by Hermes I/F) or pseudo-RID (as defined by WCI)
 *
 *.RETURNS
 *   HCF_SUCCESS
 *!! via cmd_exe
 *   HCF_ERR_NO_NIC      NIC removed during data retrieval
 *   HCF_ERR_TIME_OUT    Expected F/W event did not occur in time
 *   HCF_ERR_DEFUNCT_...
 *!! via download                CFG_DLNV_START <= type <= CFG_DL_STOP
 *!! via put_info                CFG_RID_CFG_MIN <= type <= CFG_RID_CFG_MAX
 *!! via put_frag
 *
 *.DESCRIPTION
 * The L-field of the LTV-record (provided by the MSF in parameter ltvp) specifies the size of the buffer.
 * The L-value includes the size of the T-field, but not the size of the L-field.
 * The T- field specifies the RID placed in the V-field by the MSF.
 *
 * Not all CFG-codes can be used for hcf_put_info.  The following CFG-codes are valid for hcf_put_info:
 * o One of the CFG-codes in the group "Network Parameters, Static Configuration Entities"
 * Changes made by hcf_put_info to CFG_codes in this group will not affect the F/W
 * and HCF behavior until hcf_cntl_port( HCF_PORT_ENABLE) is called.
 * o One of the CFG-codes in the group "Network Parameters, Dynamic Configuration Entities"
 * Changes made by hcf_put_info to CFG_codes will affect the F/W and HCF behavior immediately.
 * o CFG_PROG.
 * This code is used to initiate and terminate the process to download data either to
 * volatile or to non-volatile RAM on the NIC as well as for the actual download.
 * o CFG-codes related to the HCF behavior.
 * The related CFG-codes are:
 *  - CFG_REG_MB
 *  - CFG_REG_ASSERT_RTNP
 *  - CFG_REG_INFO_LOG
 *  - CFG_CMD_NIC
 *  - CFG_CMD_DONGLE
 *  - CFG_CMD_HCF
 *  - CFG_NOTIFY
 *
 * All LTV-records "unknown" to the HCF are forwarded to the F/W.
 *
 * Assert fails if
 * - ifbp has a recognizable out-of-range value.
 * - ltvp is a NULL pointer.
 * - hcf_put_info was called without prior call to hcf_connect
 * - type field of the LTV-record is invalid, i.e. neither HCF nor F/W can handle the value.
 * - length field of the LTV-record at entry is less than 1 or exceeds MAX_LTV_SIZE.
 * - registering a MailBox with size less than 60 or a non-aligned buffer address is used.
 * - reentrancy, may be  caused by calling hcf_functions without adequate protection against
 *   NIC interrupts or multi-threading.
 *
 *.DIAGRAM
 *
 *.NOTICE
 *   Remarks:  In case of Hermes Configuration LTVs, the codes for the type are "cleverly" chosen to be
 *   identical to the RID. Hermes Configuration information is copied from the provided data structure into the
 *   Card.
 *   In case of HCF Configuration LTVs, the type values are chosen in a range which does not overlap the
 *   RID-range.
 *
 *20:
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/

int
hcf_put_info( IFBP ifbp, LTVP ltvp )
{
	int rc = HCF_SUCCESS;

	HCFLOGENTRY( HCF_TRACE_PUT_INFO, ltvp->typ );
	HCFASSERT( ifbp->IFB_Magic == HCF_MAGIC, ifbp->IFB_Magic );
	HCFASSERT_INT;
	HCFASSERT( ltvp, 0 );
	HCFASSERT( 1 < ltvp->len && ltvp->len <= HCF_MAX_LTV + 1, ltvp->len );

	                                        //all codes between 0xFA00 and 0xFCFF are passed to Hermes
#if (HCF_TYPE) & HCF_TYPE_WPA
	{
		hcf_16 i;
		hcf_32 FAR * key_p;

		if ( ltvp->typ == CFG_ADD_TKIP_DEFAULT_KEY || ltvp->typ == CFG_ADD_TKIP_MAPPED_KEY ) {
			key_p = (hcf_32*)((CFG_ADD_TKIP_MAPPED_KEY_STRCT FAR *)ltvp)->tx_mic_key;
			i = TX_KEY;     //i.e. TxKeyIndicator == 1, KeyID == 0
			if ( ltvp->typ == CFG_ADD_TKIP_DEFAULT_KEY ) {
				key_p = (hcf_32*)((CFG_ADD_TKIP_DEFAULT_KEY_STRCT FAR *)ltvp)->tx_mic_key;
				i = CNV_LITTLE_TO_SHORT(((CFG_ADD_TKIP_DEFAULT_KEY_STRCT FAR *)ltvp)->tkip_key_id_info);
			}
			if ( i & TX_KEY ) { /* TxKeyIndicator == 1
					       (either really set by MSF in case of DEFAULT or faked by HCF in case of MAPPED ) */
				ifbp->IFB_MICTxCntl = (hcf_16)( HFS_TX_CNTL_MIC | (i & KEY_ID )<<8 );
				ifbp->IFB_MICTxKey[0] = CNV_LONGP_TO_LITTLE( key_p );
				ifbp->IFB_MICTxKey[1] = CNV_LONGP_TO_LITTLE( (key_p+1) );
			}
			i = ( i & KEY_ID ) * 2;
			ifbp->IFB_MICRxKey[i]   = CNV_LONGP_TO_LITTLE( (key_p+2) );
			ifbp->IFB_MICRxKey[i+1] = CNV_LONGP_TO_LITTLE( (key_p+3) );
		}
#define P ((CFG_REMOVE_TKIP_DEFAULT_KEY_STRCT FAR *)ltvp)
		if ( ( ltvp->typ == CFG_REMOVE_TKIP_MAPPED_KEY )    ||
		     ( ltvp->typ == CFG_REMOVE_TKIP_DEFAULT_KEY &&
		       ( (ifbp->IFB_MICTxCntl >> 8) & KEY_ID ) == CNV_SHORT_TO_LITTLE(P->tkip_key_id )
			     )
			) { ifbp->IFB_MICTxCntl = 0; }      //disable MIC-engine
#undef P
	}
#endif // HCF_TYPE_WPA

	if ( ltvp->typ == CFG_PROG ) {
		rc = download( ifbp, (CFG_PROG_STRCT FAR *)ltvp );
	} else switch (ltvp->typ) {
#if (HCF_ASSERT) & HCF_ASSERT_RT_MSF_RTN
		case CFG_REG_ASSERT_RTNP:                                         //Register MSF Routines
#define P ((CFG_REG_ASSERT_RTNP_STRCT FAR *)ltvp)
			ifbp->IFB_AssertRtn = P->rtnp;
//			ifbp->IFB_AssertLvl = P->lvl;       //TODO not yet supported so default is set in hcf_connect
			HCFASSERT( DO_ASSERT, MERGE_2( HCF_ASSERT, 0xCAF1 ) );   //just to proof that the complete assert machinery is working
#undef P
			break;
#endif // HCF_ASSERT_RT_MSF_RTN
#if (HCF_EXT) & HCF_EXT_INFO_LOG
		case CFG_REG_INFO_LOG:                                            //Register Log filter
			ifbp->IFB_RIDLogp = ((CFG_RID_LOG_STRCT FAR*)ltvp)->recordp;
			break;
#endif // HCF_EXT_INFO_LOG
		case CFG_CNTL_OPT:                                                //overrule option
			HCFASSERT( ( ltvp->val[0] & ~(USE_DMA | USE_16BIT) ) == 0, ltvp->val[0] );
			if ( ( ltvp->val[0] & USE_DMA ) == 0 ) ifbp->IFB_CntlOpt &= ~USE_DMA;
			ifbp->IFB_CntlOpt |=  ltvp->val[0] & USE_16BIT;
			break;

		case CFG_REG_MB:                                                  //Register MailBox
#define P ((CFG_REG_MB_STRCT FAR *)ltvp)
			HCFASSERT( ( (hcf_32)P->mb_addr & 0x0001 ) == 0, (hcf_32)P->mb_addr );
			HCFASSERT( (P)->mb_size >= 60, (P)->mb_size );
			ifbp->IFB_MBp = P->mb_addr;
			/* if no MB present, size must be 0 for ;?the old;? put_info_mb to work correctly */
			ifbp->IFB_MBSize = ifbp->IFB_MBp == NULL ? 0 : P->mb_size;
			ifbp->IFB_MBWp = ifbp->IFB_MBRp = 0;
			ifbp->IFB_MBp[0] = 0;                                           //flag the MailBox as empty
			ifbp->IFB_MBInfoLen = 0;
			HCFASSERT( ifbp->IFB_MBSize >= 60 || ifbp->IFB_MBp == NULL, ifbp->IFB_MBSize );
#undef P
			break;
		case CFG_MB_INFO:                                                 //store MailBoxInfoBlock
			rc = put_info_mb( ifbp, (CFG_MB_INFO_STRCT FAR *)ltvp );
			break;

#if (HCF_EXT) & HCF_EXT_NIC_ACCESS
		case CFG_CMD_NIC:
#define P ((CFG_CMD_NIC_STRCT FAR *)ltvp)
			OPW( HREG_PARAM_2, P->parm2 );
			OPW( HREG_PARAM_1, P->parm1 );
			rc = cmd_exe( ifbp, P->cmd, P->parm0 );
			P->hcf_stat = (hcf_16)rc;
			P->stat = IPW( HREG_STAT );
			P->resp0 = IPW( HREG_RESP_0 );
			P->resp1 = IPW( HREG_RESP_1 );
			P->resp2 = IPW( HREG_RESP_2 );
			P->ifb_err_cmd = ifbp->IFB_ErrCmd;
			P->ifb_err_qualifier = ifbp->IFB_ErrQualifier;
#undef P
			break;
		case CFG_CMD_HCF:
#define P ((CFG_CMD_HCF_STRCT FAR *)ltvp)
			HCFASSERT( P->cmd == CFG_CMD_HCF_REG_ACCESS, P->cmd );       //only Hermes register access supported
			if ( P->cmd == CFG_CMD_HCF_REG_ACCESS ) {
				HCFASSERT( P->mode < ifbp->IFB_IOBase, P->mode );        //Check Register space
				OPW( P->mode, P->add_info);
			}
#undef P
			break;
#endif // HCF_EXT_NIC_ACCESS

#if (HCF_ASSERT) & HCF_ASSERT_PRINTF
		case CFG_FW_PRINTF_BUFFER_LOCATION:
			ifbp->IFB_FwPfBuff = *(CFG_FW_PRINTF_BUFFER_LOCATION_STRCT*)ltvp;
			break;
#endif // HCF_ASSERT_PRINTF

		default:                      //pass everything unknown above the "FID" range to the Hermes or Dongle
			rc = put_info( ifbp, ltvp );
		}
	//DO NOT !!! HCFASSERT( rc == HCF_SUCCESS, rc )                                             /* 20 */
	HCFLOGEXIT( HCF_TRACE_PUT_INFO );
	return rc;
} // hcf_put_info


/************************************************************************************************************
 *
 *.MODULE        int hcf_rcv_msg( IFBP ifbp, DESC_STRCT *descp, unsigned int offset )
 *.PURPOSE       All: decapsulate a message.
 *               pre-HermesII.5: verify MIC.
 *               non-USB, non-DMA mode: Transfer a message from the NIC to the Host and acknowledge reception.
 *               USB: Transform a message from proprietary USB format to 802.3 format
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   descp       Pointer to the Descriptor List location.
 *   offset      USB: not used
 *               non-USB: specifies the beginning of the data to be obtained (0 corresponds with DestAddr field
 *               of frame).
 *
 *.RETURNS
 *   HCF_SUCCESS         No WPA error ( or HCF_ERR_MIC already reported by hcf_service_nic)
 *   HCF_ERR_MIC         message contains an erroneous MIC ( HCF_SUCCESS is reported if HCF_ERR_MIC is already
 *                       reported by hcf_service_nic)
 *   HCF_ERR_NO_NIC      NIC removed during data retrieval
 *   HCF_ERR_DEFUNCT...
 *
 *.DESCRIPTION
 * The Receive Message Function can be executed by the MSF to obtain the Data Info fields of the message that
 * is reported to be available by the Service NIC Function.
 *
 * The Receive Message Function copies the message data available in the Card memory into a buffer structure
 * provided by the MSF.
 * Only data of the message indicated by the Service NIC Function can be obtained.
 * Execution of the Service NIC function may result in the availability of a new message, but it definitely
 * makes the message reported by the preceding Service NIC function, unavailable.
 *
 * in non-USB/non-DMA mode, hcf_rcv_msg starts the copy process at the (non-negative) offset requested by the
 * parameter offset, relative to HFS_ADDR_DEST, e.g offset 0 starts copying from the Destination Address, the
 * very begin of the 802.3 frame message. Offset must either lay within the part of the 802.3 frame as stored
 * by hcf_service_nic in the lookahead buffer or be just behind it, i.e. the first byte not yet read.
 * When offset is within lookahead, data is copied from lookahead.
 * When offset is beyond lookahead, data is read directly from RxFS in NIC with disregard of the actual value
 * of offset
 *
 *.NOTICE:
 * o at entry: look ahead buffer as passed with hcf_service_nic is still accessible and unchanged
 * o at exit: Receive Frame in NIC memory is released
 *
 * Description:
 * Starting at the byte indicated by the Offset value, the bytes are copied from the Data Info
 * Part of the current Receive Frame Structure to the Host memory data buffer structure
 * identified by descp.
 * The maximum value for Offset is the number of characters of the 802.3 frame read into the
 * look ahead buffer by hcf_service_nic (i.e. the look ahead buffer size minus
 * Control and 802.11 fields)
 * If Offset is less than the maximum value, copying starts from the look ahead buffer till the
 * end of that buffer is reached
 * Then (or if the maximum value is specified for Offset), the
 * message is directly copied from NIC memory to Host memory.
 * If an invalid (i.e. too large) offset is specified, an assert catches but the buffer contents are
 * undefined.
 * Copying stops if either:
 * o the end of the 802.3 frame is reached
 * o the Descriptor with a NULL pointer in the next_desc_addr field is reached
 *
 * When the copying stops, the receiver is ack'ed, thus freeing the NIC memory where the frame is stored
 * As a consequence, hcf_rcv_msg can only be called once for any particular Rx frame.
 *
 * For the time being (PCI Bus mastering not yet supported), only the following fields of each
 * of the descriptors in the descriptor list must be set by the MSF:
 * o buf_cntl.buf_dim[1]
 * o *next_desc_addr
 * o *buf_addr
 * At return from hcf_rcv_msg, the field buf_cntl.buf_dim[0] of the used Descriptors reflects
 * the number of bytes in the buffer corresponding with the Descriptor.
 * On the last used Descriptor, buf_cntl.buf_dim[0] is less or equal to buf_cntl.buf_dim[1].
 * On all preceding Descriptors buf_cntl.buf_dim[0] is equal to buf_cntl.buf_dim[1].
 * On all succeeding (unused) Descriptors, buf_cntl.buf_dim[0] is zero.
 * Note: this I/F is based on the assumptions how the I/F needed for PCI Bus mastering will
 * be, so it may change.
 *
 * The most likely handling of HCF_ERR_NO_NIC by the MSF is to drop the already copied
 * data as elegantly as possible under the constraints and requirements posed by the (N)OS.
 * If no received Frame Structure is pending, "Success" rather than "Read error" is returned.
 * This error constitutes a logic flaw in the MSF
 * The HCF can only catch a minority of this
 * type of errors
 * Based on consistency ideas, the HCF catches none of these errors.
 *
 * Assert fails if
 * - ifbp has a recognizable out-of-range value
 * - there is no unacknowledged Rx-message available
 * - offset is out of range (outside look ahead buffer)
 * - descp is a NULL pointer
 * - any of the descriptors is not double word aligned
 * - reentrancy, may be  caused by calling hcf_functions without adequate protection
 *   against NIC interrupts or multi-threading.
 * - Interrupts are enabled.
 *
 *.DIAGRAM
 *
 *.NOTICE
 * - by using unsigned int as type for offset, no need to worry about negative offsets
 * - Asserting on being enabled/present is superfluous, since a non-zero IFB_lal implies that hcf_service_nic
 *   was called and detected a Rx-message. A zero IFB_lal will set the BUF_CNT field of at least the first
 *   descriptor to zero.
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
int
hcf_rcv_msg( IFBP ifbp, DESC_STRCT *descp, unsigned int offset )
{
	int         rc = HCF_SUCCESS;
	wci_bufp    cp;                                     //char oriented working pointer
	hcf_16      i;
	int         tot_len = ifbp->IFB_RxLen - offset;     //total length
	wci_bufp    lap = ifbp->IFB_lap + offset;           //start address in LookAhead Buffer
	hcf_16      lal = ifbp->IFB_lal - offset;           //available data within LookAhead Buffer
	hcf_16      j;

	HCFLOGENTRY( HCF_TRACE_RCV_MSG, offset );
	HCFASSERT( ifbp->IFB_Magic == HCF_MAGIC, ifbp->IFB_Magic );
	HCFASSERT_INT;
	HCFASSERT( descp, HCF_TRACE_RCV_MSG );
	HCFASSERT( ifbp->IFB_RxLen, HCF_TRACE_RCV_MSG );
	HCFASSERT( ifbp->IFB_RxLen >= offset, MERGE_2( offset, ifbp->IFB_RxLen ) );
	HCFASSERT( ifbp->IFB_lal >= offset, offset );
	HCFASSERT( (ifbp->IFB_CntlOpt & USE_DMA) == 0, 0xDADA );

	if ( tot_len < 0 ) {
		lal = 0; tot_len = 0;               //suppress all copying activity in the do--while loop
	}
	do {                                    //loop over all available fragments
		// obnoxious hcf.c(1480) : warning C4769: conversion of near pointer to long integer
		HCFASSERT( ((hcf_32)descp & 3 ) == 0, (hcf_32)descp );
		cp = descp->buf_addr;
		j = min( (hcf_16)tot_len, descp->BUF_SIZE );    //minimum of "what's` available" and fragment size
		descp->BUF_CNT = j;
		tot_len -= j;                       //adjust length still to go
		if ( lal ) {                        //if lookahead Buffer not yet completely copied
			i = min( lal, j );              //minimum of "what's available" in LookAhead and fragment size
			lal -= i;                       //adjust length still available in LookAhead
			j -= i;                         //adjust length still available in current fragment
			/*;? while loop could be improved by moving words but that is complicated on platforms with
			 * alignment requirements*/
			while ( i-- ) *cp++ = *lap++;
		}
		if ( j ) {  //if LookAhead Buffer exhausted but still space in fragment, copy directly from NIC RAM
			get_frag( ifbp, cp, j BE_PAR(0) );
			CALC_RX_MIC( cp, j );
		}
	} while ( ( descp = descp->next_desc_addr ) != NULL );
#if (HCF_TYPE) & HCF_TYPE_WPA
	if ( ifbp->IFB_RxFID ) {
		rc = check_mic( ifbp );             //prevents MIC error report if hcf_service_nic already consumed all
	}
#endif // HCF_TYPE_WPA
	(void)hcf_action( ifbp, HCF_ACT_RX_ACK );       //only 1 shot to get the data, so free the resources in the NIC
	HCFASSERT( rc == HCF_SUCCESS, rc );
	HCFLOGEXIT( HCF_TRACE_RCV_MSG );
	return rc;
} // hcf_rcv_msg


/************************************************************************************************************
 *
 *.MODULE        int hcf_send_msg( IFBP ifbp, DESC_STRCT *descp, hcf_16 tx_cntl )
 *.PURPOSE       Encapsulate a message and append padding and MIC.
 *               non-USB: Transfers the resulting message from Host to NIC and initiates transmission.
 *               USB: Transfer resulting message into a flat buffer.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   descp       pointer to the DescriptorList or NULL
 *   tx_cntl     indicates MAC-port and (Hermes) options
 *                   HFS_TX_CNTL_SPECTRALINK
 *                   HFS_TX_CNTL_PRIO
 *                   HFS_TX_CNTL_TX_OK
 *                   HFS_TX_CNTL_TX_EX
 *                   HFS_TX_CNTL_TX_DELAY
 *                   HFS_TX_CNTL_TX_CONT
 *                   HCF_PORT_0               MAC Port 0 (default)
 *                   HCF_PORT_1 (AP only)     MAC Port 1
 *                   HCF_PORT_2 (AP only)     MAC Port 2
 *                   HCF_PORT_3 (AP only)     MAC Port 3
 *                   HCF_PORT_4 (AP only)     MAC Port 4
 *                   HCF_PORT_5 (AP only)     MAC Port 5
 *                   HCF_PORT_6 (AP only)     MAC Port 6
 *
 *.RETURNS
 *   HCF_SUCCESS
 *   HCF_ERR_DEFUNCT_..
 *   HCF_ERR_TIME_OUT
 *
 *.DESCRIPTION:
 * The Send Message Function embodies 2 functions:
 * o transfers a message (including MAC header) from the provided buffer structure in Host memory to the Transmit
 * Frame Structure (TxFS) in NIC memory.
 * o Issue a send command to the F/W to actually transmit the contents of the TxFS.
 *
 * Control is based on the Resource Indicator IFB_RscInd.
 * The Resource Indicator is maintained by the HCF and should only be interpreted but not changed by the MSF.
 * The MSF must check IFB_RscInd to be non-zero before executing the call to the Send Message Function.
 * When no resources are available, the MSF must handle the queuing of the Transmit frame and check the
 * Resource Indicator periodically after calling hcf_service_nic.
 *
 * The Send Message Functions transfers a message to NIC memory when it is called with a non-NULL descp.
 * Before the Send Message Function is invoked this way, the Resource Indicator (IFB_RscInd) must be checked.
 * If the Resource is not available, Send Message Function execution must be postponed until after processing of
 * a next hcf_service_nic it appears that the Resource has become available.
 * The message is copied from the buffer structure identified by descp to the NIC.
 * Copying stops if a NULL pointer in the next_desc_addr field is reached.
 * Hcf_send_msg does not check for transmit buffer overflow, because the F/W does this protection.
 * In case of a transmit buffer overflow, the surplus which does not fit in the buffer is simply dropped.
 *
 * The Send Message Function activates the F/W to actually send the message to the medium when the
 * HFS_TX_CNTL_TX_DELAY bit of the tx_cntl parameter is not set.
 * If the descp parameter of the current call is non-NULL, the message as represented by descp is send.
 * If the descp parameter of the current call is NULL, and if the preceding call of the Send Message Function had
 * a non-NULL descp and the preceding call had the HFS_TX_CNTL_TX_DELAY bit of tx_cntl set, then the message as
 * represented by the descp of the preceding call is send.
 *
 * Hcf_send_msg supports encapsulation (see HCF_ENCAP) of Ethernet-II frames.
 * An Ethernet-II frame is transferred to the Transmit Frame structure as an 802.3 frame.
 * Hcf_send_msg distinguishes between an 802.3 and an Ethernet-II frame by looking at the data length/type field
 * of the frame. If this field contains a value larger than 1514, the frame is considered to be an Ethernet-II
 * frame, otherwise it is treated as an 802.3 frame.
 * To ease implementation of the HCF, this type/type field must be located in the first descriptor structure,
 * i.e. the 1st fragment must have a size of at least 14 (to contain DestAddr, SrcAddr and Len/Type field).
 * An Ethernet-II frame is encapsulated by inserting a SNAP header between the addressing information and the
 * type field.  This insertion is transparent for the MSF.
 * The HCF contains a fixed table that stores a number of types. If the value specified by the type/type field
 * occurs in this table, Bridge Tunnel Encapsulation is used, otherwise RFC1042 encapsulation is used.
 * Bridge Tunnel uses    AA AA 03 00 00 F8 as SNAP header,
 * RFC1042 uses  AA AA 03 00 00 00 as SNAP header.
 * The table currently contains:
 * 0 0x80F3  AppleTalk Address Resolution Protocol (AARP)
 * 0 0x8137  IPX
 *
 * The algorithm to distinguish between 802.3 and Ethernet-II frames limits the maximum length for frames of
 * 802.3 frames to 1514 bytes.
 * Encapsulation can be suppressed by means of the system constant HCF_ENCAP, e.g. to support proprietary
 * protocols with 802.3 like frames with a size larger than 1514 bytes.
 *
 * In case the HCF encapsulates the frame, the number of bytes that is actually transmitted is determined by the
 * cumulative value of the buf_cntl.buf_dim[0] fields.
 * In case the HCF does not encapsulate the frame, the number of bytes that is actually transmitted is not
 * determined by the cumulative value of the buf_cntl.buf_dim[DESC_CNTL_CNT] fields of the desc_strct's but by
 * the Length field of the 802.3 frame.
 * If there is a conflict between the cumulative value of the buf_cntl.buf_dim[0] fields and the
 * 802.3 Length field the 802.3 Length field determines the number of bytes actually transmitted by the NIC while
 * the cumulative value of the buf_cntl.buf_dim[0] fields determines the position of the MIC, hence a mismatch
 * will result in MIC errors on the Receiving side.
 * Currently this problem is flagged on the Transmit side by an Assert.
 * The following fields of each of the descriptors in the descriptor list must be set by the MSF:
 * o buf_cntl.buf_dim[0]
 * o *next_desc_addr
 * o *buf_addr
 *
 * All bits of the tx_cntl parameter except HFS_TX_CNTL_TX_DELAY and the HCF_PORT# bits are passed to the F/W via
 * the HFS_TX_CNTL field of the TxFS.
 *
 * Note that hcf_send_msg does not detect NIC absence.  The MSF is supposed to have its own -platform dependent-
 * way to recognize card removal/insertion.
 * The total system must be robust against card removal and there is no principal difference between card removal
 * just after hcf_send_msg returns but before the actual transmission took place or sometime earlier.
 *
 * Assert fails if
 * - ifbp has a recognizable out-of-range value
 * - descp is a NULL pointer
 * - no resources for PIF available.
 * - Interrupts are enabled.
 * - reentrancy, may be  caused by calling hcf_functions without adequate protection
 *   against NIC interrupts or multi-threading.
 *
 *.DIAGRAM
 *4: for the normal case (i.e. no HFS_TX_CNTL_TX_DELAY option active), a fid is acquired via the
 *   routine get_fid.  If no FID is acquired, the remainder is skipped without an error notification.  After
 *   all, the MSF is not supposed to call hcf_send_msg when no Resource is available.
 *7: The ControlField of the TxFS is written.  Since put_frag can only return the fatal Defunct or "No NIC", the
 *   return status can be ignored because when it fails, cmd_wait will fail as well.  (see also the note on the
 *   need for a return code below).
 *   Note that HFS_TX_CNTL has different values for H-I, H-I/WPA and H-II and HFS_ADDR_DEST has different
 *   values for H-I (regardless of WPA) and H-II.
 *   By writing 17, 1 or 2 ( implying 16, 0 or 1 garbage word after HFS_TX_CNTL) the BAP just gets to
 *   HFS_ADDR_DEST for H-I, H-I/WPA and H-II respectively.
 *10: if neither encapsulation nor MIC calculation is needed, splitting the first fragment in two does not
 *   really help but it makes the flow easier to follow to do not optimize on this difference
 *
 *   hcf_send_msg checks whether the frame is an Ethernet-II rather than an "official" 802.3 frame.
 *   The E-II check is based on the length/type field in the MAC header. If this field has a value larger than
 *   1500, E-II is assumed. The implementation of this test fails if the length/type field is not in the first
 *   descriptor.  If E-II is recognized, a SNAP header is inserted. This SNAP header represents either RFC1042
 *   or Bridge-Tunnel encapsulation, depending on the return status of the support routine hcf_encap.
 *
 *.NOTICE
 *   hcf_send_msg leaves the responsibility to only send messages on enabled ports at the MSF level.
 *   This is considered the strategy which is sufficiently adequate for all "robust" MSFs, have the least
 *   processor utilization and being still acceptable robust at the WCI !!!!!
 *
 *   hcf_send_msg does not NEED a return value to report NIC absence or removal during the execution of
 *   hcf_send_msg(), because the MSF and higher layers must be able to cope anyway with the NIC being removed
 *   after a successful completion of hcf_send_msg() but before the actual transmission took place.
 *   To accommodate user expectations the current implementation does report NIC absence.
 *   Defunct blocks all NIC access and will (also) be reported on a number of other calls.
 *
 *   hcf_send_msg does not check for transmit buffer overflow because the Hermes does this protection.
 *   In case of a transmit buffer overflow, the surplus which does not fit in the buffer is simply dropped.
 *   Note that this possibly results in the transmission of incomplete frames.
 *
 *   After some deliberation with F/W team, it is decided that - being in the twilight zone of not knowing
 *   whether the problem at hand is an MSF bug, HCF buf, F/W bug, H/W malfunction or even something else - there
 *   is no "best thing to do" in case of a failing send, hence the HCF considers the TxFID ownership to be taken
 *   over by the F/W and hopes for an Allocate event in due time
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
int
hcf_send_msg( IFBP ifbp, DESC_STRCT *descp, hcf_16 tx_cntl )
{
	int         rc = HCF_SUCCESS;
	DESC_STRCT  *p /* = descp*/;        //working pointer
	hcf_16      len;                    // total byte count
	hcf_16      i;

	hcf_16      fid = 0;

	HCFASSERT( ifbp->IFB_RscInd || descp == NULL, ifbp->IFB_RscInd );
	HCFASSERT( (ifbp->IFB_CntlOpt & USE_DMA) == 0, 0xDADB );

	HCFLOGENTRY( HCF_TRACE_SEND_MSG, tx_cntl );
	HCFASSERT( ifbp->IFB_Magic == HCF_MAGIC, ifbp->IFB_Magic );
	HCFASSERT_INT;
	/* obnoxious c:/hcf/hcf.c(1480) : warning C4769: conversion of near pointer to long integer,
	 * so skip */
	HCFASSERT( ((hcf_32)descp & 3 ) == 0, (hcf_32)descp );
#if HCF_ASSERT
	{   int x = ifbp->IFB_FWIdentity.comp_id == COMP_ID_FW_AP ? tx_cntl & ~HFS_TX_CNTL_PORT : tx_cntl;
		HCFASSERT( (x & ~HCF_TX_CNTL_MASK ) == 0, tx_cntl );
	}
#endif // HCF_ASSERT

	if ( descp ) ifbp->IFB_TxFID = 0;               //cancel a pre-put message

	/* the following initialization code is redundant for a pre-put message
	 * but moving it inside the "if fid" logic makes the merging with the
	 * USB flow awkward
	 */
#if (HCF_TYPE) & HCF_TYPE_WPA
	tx_cntl |= ifbp->IFB_MICTxCntl;
#endif // HCF_TYPE_WPA
	fid = ifbp->IFB_TxFID;
	if (fid == 0 && ( fid = get_fid( ifbp ) ) != 0 )        /* 4 */
		/* skip the next compound statement if:
		   - pre-put message or
		   - no fid available (which should never occur if the MSF adheres to the WCI)
		*/
	{       // to match the closing curly bracket of above "if" in case of HCF_TYPE_USB
		                                //calculate total length ;? superfluous unless CCX or Encapsulation
		len = 0;
		p = descp;
		do len += p->BUF_CNT; while ( ( p = p->next_desc_addr ) != NULL );
		p = descp;
//;?		HCFASSERT( len <= HCF_MAX_MSG, len );
	/*7*/   (void)setup_bap( ifbp, fid, HFS_TX_CNTL, IO_OUT );
#if (HCF_TYPE) & HCF_TYPE_TX_DELAY
		HCFASSERT( ( descp != NULL ) ^ ( tx_cntl & HFS_TX_CNTL_TX_DELAY ), tx_cntl );
		if ( tx_cntl & HFS_TX_CNTL_TX_DELAY ) {
			tx_cntl &= ~HFS_TX_CNTL_TX_DELAY;       //!!HFS_TX_CNTL_TX_DELAY no longer available
			ifbp->IFB_TxFID = fid;
			fid = 0;                                //!!fid no longer available, be careful when modifying code
		}
#endif // HCF_TYPE_TX_DELAY
		OPW( HREG_DATA_1, tx_cntl ) ;
		OPW( HREG_DATA_1, 0 );

		HCFASSERT( p->BUF_CNT >= 14, p->BUF_CNT );
		                                /* assume DestAddr/SrcAddr/Len/Type ALWAYS contained in 1st fragment
		                                 * otherwise life gets too cumbersome for MIC and Encapsulation !!!!!!!!
		 if ( p->BUF_CNT >= 14 ) {   alternatively: add a safety escape !!!!!!!!!!!! }   */

		CALC_TX_MIC( NULL, -1 );        //initialize MIC
	/*10*/  put_frag( ifbp, p->buf_addr, HCF_DASA_SIZE BE_PAR(0) ); //write DA, SA with MIC calculation
		CALC_TX_MIC( p->buf_addr, HCF_DASA_SIZE );      //MIC over DA, SA
		CALC_TX_MIC( null_addr, 4 );        //MIC over (virtual) priority field

			                        //if encapsulation needed
#if (HCF_ENCAP) == HCF_ENC
			                        //write length (with SNAP-header,Type, without //DA,SA,Length ) no MIC calc.
		if ( ( snap_header[sizeof(snap_header)-1] = hcf_encap( &p->buf_addr[HCF_DASA_SIZE] ) ) != ENC_NONE ) {
			OPW( HREG_DATA_1, CNV_END_SHORT( len + (sizeof(snap_header) + 2) - ( 2*6 + 2 ) ) );
				                //write splice with MIC calculation
			put_frag( ifbp, snap_header, sizeof(snap_header) BE_PAR(0) );
			CALC_TX_MIC( snap_header, sizeof(snap_header) );    //MIC over 6 byte SNAP
			i = HCF_DASA_SIZE;
		} else
#endif // HCF_ENC
		{
			OPW( HREG_DATA_1, *(wci_recordp)&p->buf_addr[HCF_DASA_SIZE] );
			i = 14;
		}
			                        //complete 1st fragment starting with Type with MIC calculation
		put_frag( ifbp, &p->buf_addr[i], p->BUF_CNT - i BE_PAR(0) );
		CALC_TX_MIC( &p->buf_addr[i], p->BUF_CNT - i );

		                                //do the remaining fragments with MIC calculation
		while ( ( p = p->next_desc_addr ) != NULL ) {
			/* obnoxious c:/hcf/hcf.c(1480) : warning C4769: conversion of near pointer to long integer,
			 * so skip */
			HCFASSERT( ((hcf_32)p & 3 ) == 0, (hcf_32)p );
			put_frag( ifbp, p->buf_addr, p->BUF_CNT BE_PAR(0) );
			CALC_TX_MIC( p->buf_addr, p->BUF_CNT );
		}
		                                //pad message, finalize MIC calculation and write MIC to NIC
		put_frag_finalize( ifbp );
	}
	if ( fid ) {
	/*16*/  rc = cmd_exe( ifbp, HCMD_BUSY | HCMD_TX | HCMD_RECL, fid );
		ifbp->IFB_TxFID = 0;
		/* probably this (i.e. no RscInd AND "HREG_EV_ALLOC") at this point in time occurs so infrequent,
		 * that it might just as well be acceptable to skip this
		 * "optimization" code and handle that additional interrupt once in a while
		 */
// 180 degree error in logic ;? #if ALLOC_15
	/*20*/  if ( ifbp->IFB_RscInd == 0 ) {
			ifbp->IFB_RscInd = get_fid( ifbp );
		}
// #endif // ALLOC_15
	}
//	HCFASSERT( level::ifbp->IFB_RscInd, ifbp->IFB_RscInd );
	HCFLOGEXIT( HCF_TRACE_SEND_MSG );
	return rc;
} // hcf_send_msg


/************************************************************************************************************
 *
 *.MODULE        int hcf_service_nic( IFBP ifbp, wci_bufp bufp, unsigned int len )
 *.PURPOSE       Services (most) NIC events.
 *               Provides received message
 *               Provides status information.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *  In non-DMA mode:
 *   bufp        address of char buffer, sufficiently large to hold the first part of the RxFS up through HFS_TYPE
 *   len         length in bytes of buffer specified by bufp
 *               value between HFS_TYPE + 2 and HFS_ADDR_DEST + HCF_MAX_MSG
 *
 *.RETURNS
 *   HCF_SUCCESS
 *   HCF_ERR_MIC message contains an erroneous MIC (only if frame fits completely in bufp)
 *
 *.DESCRIPTION
 *
 * MSF-accessible fields of Result Block
 * - IFB_RxLen           0 or Frame size.
 * - IFB_MBInfoLen       0 or the L-field of the oldest MBIB.
 * - IFB_RscInd
 * - IFB_HCF_Tallies     updated if a corresponding event occurred.
 * - IFB_NIC_Tallies     updated if a Tally Info frame received from the NIC.
 * - IFB_DmaPackets
 * - IFB_TxFsStat
 * - IFB_TxFsSwSup
 * - IFB_LinkStat        reflects new link status or 0x0000 if no change relative to previous hcf_service_nic call.
or
* - IFB_LinkStat        link status, 0x8000 reflects change relative to previous hcf_service_nic call.
*
* When IFB_MBInfoLen is non-zero, at least one MBIB is available.
*
* IFB_RxLen reflects the number of received bytes in 802.3 view (Including DestAddr, SrcAddr and Length,
* excluding MIC-padding, MIC and sum check) of active Rx Frame Structure. If no Rx Data s available, IFB_RxLen
* equals 0x0000.
* Repeated execution causes the Service NIC Function to provide information about subsequently received
* messages, irrespective whether a hcf_rcv_msg or hcf_action(HCF_ACT_RX) is performed in between.
*
* When IFB_RxLen is non-zero, a Received Frame Structure is available to be routed to the protocol stack.
* When Monitor Mode is not active, this is guaranteed to be an error-free non-WMP frame.
* In case of Monitor Mode, it may also be a frame with an error or a WMP frame.
* Erroneous frames have a non-zero error-sub field in the HFS_STAT field in the look ahead buffer.
*
* If a Receive message is available in NIC RAM, the Receive Frame Structure is (partly) copied from the NIC to
* the buffer identified by bufp.
* Copying stops either after len bytes or when the complete 802.3 frame is copied.
* During the copying the message is decapsulated (if appropriate).
* If the frame is read completely by hcf_service_nic (i.e. the frame fits completely in the lookahead buffer),
* the frame is automatically ACK'ed to the F/W and still available via the look ahead buffer and hcf_rcv_msg.
* Only if the frame is read completely by hcf_service_nic, hcf_service_nic checks the MIC and sets the return
* status accordingly.  In this case, hcf_rcv_msg does not check the MIC.
*
* The MIC calculation algorithm works more efficient if the length of the look ahead buffer is
* such that it fits exactly 4 n bytes of the 802.3 frame, i.e. len == HFS_ADDR_DEST + 4*n.
*
* The Service NIC Function supports the NIC event service handling process.
* It performs the appropriate actions to service the NIC, such that the event cause is eliminated and related
* information is saved.
* The Service NIC Function is executed by the MSF ISR or polling routine as first step to determine the event
* cause(s).  It is the responsibility of the MSF to perform all not directly NIC related interrupt service
* actions, e.g. in a PC environment this includes servicing the PIC, and managing the Processor Interrupt
* Enabling/Disabling.
* In case of a polled based system, the Service NIC Function must be executed "frequently".
* The Service NIC Function may have side effects related to the Mailbox and Resource Indicator (IFB_RscInd).
*
* hcf_service_nic returns:
* - The length of the data in the available MBIB (IFB_MBInfoLen)
* - Changes in the link status (IFB_LinkStat)
* - The length of the data in the available Receive Frame Structure (IFB_RxLen)
* - updated IFB_RscInd
* - Updated Tallies
*
* hcf_service_nic is presumed to neither interrupt other HCF-tasks nor to be interrupted by other HCF-tasks.
* A way to achieve this is to precede hcf_service_nic as well as all other HCF-tasks with a call to
* hcf_action to disable the card interrupts and, after all work is completed, with a call to hcf_action to
* restore (which is not necessarily the same as enabling) the card interrupts.
* In case of a polled environment, it is assumed that the MSF programmer is sufficiently familiar with the
* specific requirements of that environment to translate the interrupt strategy to a polled strategy.
*
* hcf_service_nic services the following Hermes events:
* - HREG_EV_INFO        Asynchronous Information Frame
* - HREG_EV_INFO_DROP   WMAC did not have sufficient RAM to build Unsolicited Information Frame
* - HREG_EV_TX_EXC      (if applicable, i.e. selected via HCF_EXT_INT_TX_EX bit of HCF_EXT)
* - HREG_EV_SLEEP_REQ   (if applicable, i.e. selected via HCF_DDS/HCF_CDS bit of HCF_SLEEP)
* ** in non_DMA mode
* - HREG_EV_ALLOC       Asynchronous part of Allocation/Reclaim completed while out of resources at
*                       completion of hcf_send_msg/notify
* - HREG_EV_RX          the detection of the availability of received messages
*                       including WaveLAN Management Protocol (WMP) message processing
* ** in DMA mode
* - HREG_EV_RDMAD
* - HREG_EV_TDMAD
*!! hcf_service_nic does not service the following Hermes events:
*!!     HREG_EV_TX          (the "OK" Tx Event) is no longer supported by the WCI, if it occurs it is unclear
*!!                         what the cause is, so no meaningful strategy is available. Not acking the bit is
*!!                         probably the best help that can be given to the debugger.
*!!     HREG_EV_CMD         handled in cmd_wait.
*!!     HREG_EV_FW_DMA      (i.e. HREG_EV_RXDMA, HREG_EV_TXDMA and_EV_LPESC) are either not used or used
*!!                         between the F/W and the DMA engine.
*!!     HREG_EV_ACK_REG_READY is only applicable for H-II (i.e. not HII.5 and up, see DAWA)
*
*   If, in non-DMA mode, a Rx message is available, its length is reflected by the IFB_RxLen field of the IFB.
*   This length reflects the data itself and the Destination Address, Source Address and DataLength/Type field
*   but not the SNAP-header in case of decapsulation by the HCF.  If no message is available, IFB_RxLen is
*   zero.  Former versions of the HCF handled WMP messages and supported a "monitor" mode in hcf_service_nic,
*   which deposited certain or all Rx messages in the MailBox. The responsibility to handle these frames is
*   moved to the MSF. The HCF offers as supports hcf_put_info with CFG_MB_INFO as parameter to emulate the old
*   implementation under control of the MSF.
*
* **Rx Buffer free strategy
*   When hcf_service_nic reports the availability of a non-DMA message, the MSF can access that message by
*   means of hcf_rcv_msg. It must be prevented that the LAN Controller writes new data in the NIC buffer
*   before the MSF is finished with the current message. The NIC buffer is returned to the LAN Controller
*   when:
*    - the complete frame fits in the lookahead buffer or
*    - hcf_rcv_msg is called or
*    - hcf_action with HCF_ACT_RX is called or
*    - hcf_service_nic is called again
*   It can be reasoned that hcf_action( INT_ON ) should not be given before the MSF has completely processed
*   a reported Rx-frame. The reason is that the INT_ON action is guaranteed to cause a (Rx-)interrupt (the
*   MSF is processing a Rx-frame, hence the Rx-event bit in the Hermes register must be active). This
*   interrupt will cause hcf_service_nic to be called, which will cause the ack-ing of the "last" Rx-event
*   to the Hermes, causing the Hermes to discard the associated NIC RAM buffer.
* Assert fails if
* - ifbp is zero or other recognizable out-of-range value.
* - hcf_service_nic is called without a prior call to hcf_connect.
* - interrupts are enabled.
* - reentrancy, may be  caused by calling hcf_functions without adequate protection
*   against NIC interrupts or multi-threading.
*
*
*.DIAGRAM
*1: IFB_LinkStat is cleared, if a LinkStatus frame is received, IFB_LinkStat will be updated accordingly
*   by isr_info.
or
*1: IFB_LinkStat change indication is cleared. If a LinkStatus frame is received, IFB_LinkStat will be updated
*   accordingly by isr_info.
*2: IFB_RxLen must be cleared before the NIC presence check otherwise:
*    -  this value may stay non-zero if the NIC is pulled out at an inconvenient moment.
*    -  the RxAck on a zero-FID needs a zero-value for IFB_RxLen to work
*    Note that as side-effect of the hcf_action call, the remainder of Rx related info is re-initialized as
*    well.
*4: In case of Defunct mode, the information supplied by Hermes is unreliable, so the body of
*   hcf_service_nic is skipped. Since hcf_cntl turns into a NOP if Primary or Station F/W is incompatible,
*   hcf_service_nic is also skipped in those cases.
*   To prevent that hcf_service_nic reports bogus information to the MSF with all - possibly difficult to
*   debug - undesirable side effects, it is paramount to check the NIC presence. In former days the presence
*   test was based on the Hermes register HREG_SW_0. Since in HCF_ACT_INT_OFF is chosen for strategy based on
*   HREG_EV_STAT, this is now also used in hcf_service_nic. The motivation to change strategy is partly
*   due to inconsistent F/W implementations with respect to HREG_SW_0 manipulation around reset and download.
*   Note that in polled environments Card Removal is not detected by INT_OFF which makes the check in
*   hcf_service_nic even more important.
*8: The event status register of the Hermes is sampled
*   The assert checks for unexpected events ;?????????????????????????????????????.
*    - HREG_EV_INFO_DROP is explicitly excluded from the acceptable HREG_EV_STAT bits because it indicates
*      a too heavily loaded system.
*    - HREG_EV_ACK_REG_READY is 0x0000 for H-I (and hopefully H-II.5)
*
*
*   HREG_EV_TX_EXC is accepted (via HREG_EV_TX_EXT) if and only if HCF_EXT_INT_TX_EX set in the HCF_EXT
*   definition at compile time.
*   The following activities are handled:
*    -  Alloc events are handled by hcf_send_msg (and notify). Only if there is no "spare" resource, the
*       alloc event is superficially serviced by hcf_service_nic to create a pseudo-resource with value
*       0x001. This value is recognized by get_fid (called by hcf_send_msg and notify) where the real
*       TxFid is retrieved and the Hermes is acked and - hopefully - the "normal" case with a spare TxFid
*       in IFB_RscInd is restored.
*    -  Info drop events are handled by incrementing a tally
*    -  LinkEvent (including solicited and unsolicited tallies) are handled by procedure isr_info.
*   -   TxEx (if selected at compile time) is handled by copying the significant part of the TxFS
*       into the IFB for further processing by the MSF.
*       Note the complication of the zero-FID protection sub-scheme in DAWA.
*   Note, the Ack of all of above events is handled at the end of hcf_service_nic
*16: In case of  non-DMA ( either not compiled in or due to a run-time choice):
*   If an Rx-frame is available, first the FID of that frame is read, including the complication of the
*   zero-FID protection sub-scheme in DAWA. Note that such a zero-FID is acknowledged at the end of
*   hcf_service_nic and that this depends on the IFB_RxLen initialization in the begin of hcf_service_nic.
*   The Assert validates the HCF assumption about Hermes implementation upon which the range of
*   Pseudo-RIDs is based.
*   Then the control fields up to the start of the 802.3 frame are read from the NIC into the lookahead buffer.
*   The status field is converted to native Endianness.
*   The length is, after implicit Endianness conversion if needed, and adjustment for the 14 bytes of the
*   802.3 MAC header, stored in IFB_RxLen.
*   In MAC Monitor mode, 802.11 control frames with a TOTAL length of 14 are received, so without this
*   length adjustment, IFB_RxLen could not be used to distinguish these frames from "no frame".
*   No MIC calculation processes are associated with the reading of these Control fields.
*26: This length test feels like superfluous robustness against malformed frames, but it turned out to be
*   needed in the real (hostile) world.
*   The decapsulation check needs sufficient data to represent DA, SA, L, SNAP and Type which amounts to
*   22 bytes. In MAC Monitor mode, 802.11 control frames with a smaller length are received. To prevent
*   that the implementation goes haywire, a check on the length is needed.
*   The actual decapsulation takes place on the fly in the copying process by overwriting the SNAP header.
*   Note that in case of decapsulation the SNAP header is not passed to the MSF, hence IFB_RxLen must be
*   compensated for the SNAP header length.
*   The 22 bytes needed for decapsulation are (more than) sufficient for the exceptional handling of the
*   MIC algorithm of the L-field (replacing the 2 byte L-field with 4 0x00 bytes).
*30: The 12 in the no-WPA branch corresponds with the get_frag, the 2 with the IPW of the WPA branch
*32: If Hermes reported MIC-presence, than the MIC engine is initialized with the non-dummy MIC calculation
*   routine address and appropriate key.
*34: The 8 bytes after the DA, SA, L are read and it is checked whether decapsulation is needed i.e.:
*     - the Hermes reported Tunnel encapsulation or
*     - the Hermes reported 1042 Encapsulation and hcf_encap reports that the HCF would not have used
*       1042 as the encapsulation mechanism
*   Note that the first field of the RxFS in bufp has Native Endianness due to the conversion done by the
*   BE_PAR in get_frag.
*36: The Type field is the only word kept (after moving) of the just read 8 bytes, it is moved to the
*   L-field.  The original L-field and 6 byte SNAP header are discarded, so IFB_RxLen and buf_addr must
*   be adjusted by 8.
*40: Determine how much of the frame (starting with DA) fits in the Lookahead buffer, then read the not-yet
*   read data into the lookahead buffer.
*   If the lookahead buffer contains the complete message, check the MIC. The majority considered this
*   I/F more appropriate then have the MSF call hcf_get_data only to check the MIC.
*44: Since the complete message is copied from NIC RAM to PC RAM, the Rx can be acknowledged to the Hermes
*   to optimize the flow ( a better chance to get new Rx data in the next pass through hcf_service_nic ).
*   This acknowledgement can not be done via hcf_action( HCF_ACT_RX_ACK ) because this also clears
*   IFB_RxLEN thus corrupting the I/F to the MSF.
*;?: In case of DMA (compiled in and activated):


*54: Limiting the number of places where the F/W is acked (e.g. the merging of the Rx-ACK with the other
*   ACKs), is supposed to diminish the potential of race conditions in the F/W.
*   Note 1: The CMD event is acknowledged in cmd_cmpl
*   Note 2: HREG_EV_ACK_REG_READY is 0x0000 for H-I (and hopefully H-II.5)
*   Note 3: The ALLOC event is acknowledged in get_fid (except for the initialization flow)
*
*.NOTICE
* The Non-DMA HREG_EV_RX is handled different compared with the other F/W events.
* The HREG_EV_RX event is acknowledged by the first hcf_service_nic call after the
* hcf_service_nic call that reported the occurrence of this event.
* This acknowledgment
* makes the next Receive Frame Structure (if any) available.
* An updated IFB_RxLen
* field reflects this availability.
*
*.NOTICE
* The minimum size for Len must supply space for:
* - an F/W dependent number of bytes of Control Info field including the 802.11 Header field
* - Destination Address
* - Source Address
* - Length field
* - [ SNAP Header]
* - [ Ethernet-II Type]
* This results in 68 for Hermes-I and 80 for Hermes-II
* This way the minimum amount of information is available needed by the HCF to determine whether the frame
* must be decapsulated.
*.ENDDOC                END DOCUMENTATION
*
************************************************************************************************************/
int
hcf_service_nic( IFBP ifbp, wci_bufp bufp, unsigned int len )
{

	int         rc = HCF_SUCCESS;
	hcf_16      stat;
	wci_bufp    buf_addr;
	hcf_16      i;

	HCFLOGENTRY( HCF_TRACE_SERVICE_NIC, ifbp->IFB_IntOffCnt );
	HCFASSERT( ifbp->IFB_Magic == HCF_MAGIC, ifbp->IFB_Magic );
	HCFASSERT_INT;

	ifbp->IFB_LinkStat = 0; // ;? to be obsoleted ASAP                                              /* 1*/
	ifbp->IFB_DSLinkStat &= ~CFG_LINK_STAT_CHANGE;                                                  /* 1*/
	(void)hcf_action( ifbp, HCF_ACT_RX_ACK );                                                       /* 2*/
	if ( ifbp->IFB_CardStat == 0 && ( stat = IPW( HREG_EV_STAT ) ) != 0xFFFF ) {                    /* 4*/
/*		IF_NOT_DMA( HCFASSERT( !( stat & ~HREG_EV_BASIC_MASK, stat ) )
 *		IF_NOT_USE_DMA( HCFASSERT( !( stat & ~HREG_EV_BASIC_MASK, stat ) )
 *		IF_USE_DMA( HCFASSERT( !( stat & ~( HREG_EV_BASIC_MASK ^ ( HREG_EV_...DMA.... ), stat ) )
 */
		                                                                                        /* 8*/
		if ( ifbp->IFB_RscInd == 0 && stat & HREG_EV_ALLOC ) { //Note: IFB_RscInd is ALWAYS 1 for DMA
			ifbp->IFB_RscInd = 1;
		}
		IF_TALLY( if ( stat & HREG_EV_INFO_DROP ) { ifbp->IFB_HCF_Tallies.NoBufInfo++; } );
#if (HCF_EXT) & HCF_EXT_INT_TICK
		if ( stat & HREG_EV_TICK ) {
			ifbp->IFB_TickCnt++;
		}
#if 0 // (HCF_SLEEP) & HCF_DDS
		if ( ifbp->IFB_TickCnt == 3 && ( ifbp->IFB_DSLinkStat & CFG_LINK_STAT_CONNECTED ) == 0 ) {
			CFG_DDS_TICK_TIME_STRCT ltv;
			// 2 second period (with 1 tick uncertanty) in not-connected mode -->go into DS_OOR
			hcf_action( ifbp, HCF_ACT_SLEEP );
			ifbp->IFB_DSLinkStat |= CFG_LINK_STAT_DS_OOR; //set OutOfRange
			ltv.len = 2;
			ltv.typ = CFG_DDS_TICK_TIME;
			ltv.tick_time = ( ( ifbp->IFB_DSLinkStat & CFG_LINK_STAT_TIMER ) + 0x10 ) *64; //78 is more right
			hcf_put_info( ifbp, (LTVP)&ltv );
			printk(KERN_NOTICE "Preparing for sleep, link_status: %04X, timer : %d\n",
				ifbp->IFB_DSLinkStat, ltv.tick_time );//;?remove me 1 day
			ifbp->IFB_TickCnt++; //;?just to make sure we do not keep on printing above message
			if ( ltv.tick_time < 300 * 125 ) ifbp->IFB_DSLinkStat += 0x0010;

		}
#endif // HCF_DDS
#endif // HCF_EXT_INT_TICK
		if ( stat & HREG_EV_INFO ) {
			isr_info( ifbp );
		}
#if (HCF_EXT) & HCF_EXT_INT_TX_EX
		if ( stat & HREG_EV_TX_EXT && ( i = IPW( HREG_TX_COMPL_FID ) ) != 0 /*DAWA*/ ) {
			DAWA_ZERO_FID( HREG_TX_COMPL_FID );
			(void)setup_bap( ifbp, i, 0, IO_IN );
			get_frag( ifbp, &ifbp->IFB_TxFsStat, HFS_SWSUP BE_PAR(1) );
		}
#endif // HCF_EXT_INT_TX_EX
//!rlav DMA engine will handle the rx event, not the driver
#if HCF_DMA
		if ( !( ifbp->IFB_CntlOpt & USE_DMA ) ) //!! be aware of the logical indentations
#endif // HCF_DMA
		/*16*/  if ( stat & HREG_EV_RX && ( ifbp->IFB_RxFID = IPW( HREG_RX_FID ) ) != 0 ) { //if 0 then DAWA_ACK
				HCFASSERT( bufp, len );
				HCFASSERT( len >= HFS_DAT + 2, len );
				DAWA_ZERO_FID( HREG_RX_FID );
				HCFASSERT( ifbp->IFB_RxFID < CFG_PROD_DATA, ifbp->IFB_RxFID);
				(void)setup_bap( ifbp, ifbp->IFB_RxFID, 0, IO_IN );
				get_frag( ifbp, bufp, HFS_ADDR_DEST BE_PAR(1) );
				ifbp->IFB_lap = buf_addr = bufp + HFS_ADDR_DEST;
				ifbp->IFB_RxLen = (hcf_16)(bufp[HFS_DAT_LEN] + (bufp[HFS_DAT_LEN+1]<<8) + 2*6 + 2);
			/*26*/  if ( ifbp->IFB_RxLen >= 22 ) {  // convenient for MIC calculation (5 DWs + 1 "skipped" W)
								//.  get DA,SA,Len/Type and (SNAP,Type or 8 data bytes)
				/*30*/  get_frag( ifbp, buf_addr, 22 BE_PAR(0) );
				/*32*/  CALC_RX_MIC( bufp, -1 );        //.  initialize MIC
					CALC_RX_MIC( buf_addr, HCF_DASA_SIZE ); //.  MIC over DA, SA
					CALC_RX_MIC( null_addr, 4 );    //.  MIC over (virtual) priority field
					CALC_RX_MIC( buf_addr+14, 8 );  //.  skip Len, MIC over SNAP,Type or 8 data bytes)
					buf_addr += 22;
#if (HCF_ENCAP) == HCF_ENC
					HCFASSERT( len >= HFS_DAT + 2 + sizeof(snap_header), len );
				/*34*/  i = *(wci_recordp)&bufp[HFS_STAT] & ( HFS_STAT_MSG_TYPE | HFS_STAT_ERR );
					if ( i == HFS_STAT_TUNNEL ||
					     ( i == HFS_STAT_1042 && hcf_encap( (wci_bufp)&bufp[HFS_TYPE] ) != ENC_TUNNEL ) ) {
							                //.  copy E-II Type to 802.3 LEN field
				/*36*/  bufp[HFS_LEN  ] = bufp[HFS_TYPE  ];
						bufp[HFS_LEN+1] = bufp[HFS_TYPE+1];
							                //.  discard Snap by overwriting with data
						ifbp->IFB_RxLen -= (HFS_TYPE - HFS_LEN);
						buf_addr -= ( HFS_TYPE - HFS_LEN ); // this happens to bring us at a DW boundary of 36
					}
#endif // HCF_ENC
				}
			/*40*/  ifbp->IFB_lal = min( (hcf_16)(len - HFS_ADDR_DEST), ifbp->IFB_RxLen );
				i = ifbp->IFB_lal - ( buf_addr - ( bufp + HFS_ADDR_DEST ) );
				get_frag( ifbp, buf_addr, i BE_PAR(0) );
				CALC_RX_MIC( buf_addr, i );
#if (HCF_TYPE) & HCF_TYPE_WPA
				if ( ifbp->IFB_lal == ifbp->IFB_RxLen ) {
					rc = check_mic( ifbp );
				}
#endif // HCF_TYPE_WPA
			/*44*/  if ( len - HFS_ADDR_DEST >= ifbp->IFB_RxLen ) {
					ifbp->IFB_RxFID = 0;
				} else { /* IFB_RxFID is cleared, so  you do not get another Rx_Ack at next entry of hcf_service_nic */
					stat &= (hcf_16)~HREG_EV_RX;    //don't ack Rx if processing not yet completed
				}
			}
		// in case of DMA: signal availability of rx and/or tx packets to MSF
		IF_USE_DMA( ifbp->IFB_DmaPackets |= stat & ( HREG_EV_RDMAD | HREG_EV_TDMAD ) );
		// rlav : pending HREG_EV_RDMAD or HREG_EV_TDMAD events get acknowledged here.
	/*54*/  stat &= (hcf_16)~( HREG_EV_SLEEP_REQ | HREG_EV_CMD | HREG_EV_ACK_REG_READY | HREG_EV_ALLOC | HREG_EV_FW_DMA );
//a positive mask would be easier to understand /*54*/  stat &= (hcf_16)~( HREG_EV_SLEEP_REQ | HREG_EV_CMD | HREG_EV_ACK_REG_READY | HREG_EV_ALLOC | HREG_EV_FW_DMA );
		IF_USE_DMA( stat &= (hcf_16)~HREG_EV_RX );
		if ( stat ) {
			DAWA_ACK( stat );   /*DAWA*/
		}
	}
	HCFLOGEXIT( HCF_TRACE_SERVICE_NIC );
	return rc;
} // hcf_service_nic


/************************************************************************************************************
 ************************** H C F   S U P P O R T   R O U T I N E S ******************************************
 ************************************************************************************************************/


/************************************************************************************************************
 *
 *.SUBMODULE     void calc_mic( hcf_32* p, hcf_32 m )
 *.PURPOSE       calculate MIC on a quad byte.
 *
 *.ARGUMENTS
 *   p           address of the MIC
 *   m           32 bit value to be processed by the MIC calculation engine
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * calc_mic is the implementation of the MIC algorithm. It is a monkey-see monkey-do copy of
 * Michael::appendByte()
 * of Appendix C of ..........
 *
 *
 *.DIAGRAM
 *
 *.NOTICE
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/

#if (HCF_TYPE) & HCF_TYPE_WPA

#define ROL32( A, n ) ( ((A) << (n)) | ( ((A)>>(32-(n)))  & ( (1UL << (n)) - 1 ) ) )
#define ROR32( A, n ) ROL32( (A), 32-(n) )

#define L   *p
#define R   *(p+1)

static void
calc_mic( hcf_32* p, hcf_32 m )
{
#if HCF_BIG_ENDIAN
	m = (m >> 16) | (m << 16);
#endif // HCF_BIG_ENDIAN
	L ^= m;
	R ^= ROL32( L, 17 );
	L += R;
	R ^= ((L & 0xff00ff00) >> 8) | ((L & 0x00ff00ff) << 8);
	L += R;
	R ^= ROL32( L, 3 );
	L += R;
	R ^= ROR32( L, 2 );
	L += R;
} // calc_mic
#undef R
#undef L
#endif // HCF_TYPE_WPA



#if (HCF_TYPE) & HCF_TYPE_WPA
/************************************************************************************************************
 *
 *.SUBMODULE     void calc_mic_rx_frag( IFBP ifbp, wci_bufp p, int len )
 *.PURPOSE       calculate MIC on a single fragment.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   bufp        (byte) address of buffer
 *   len         length in bytes of buffer specified by bufp
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * calc_mic_rx_frag ........
 *
 * The MIC is located in the IFB.
 * The MIC is separate for Tx and Rx, thus allowing hcf_send_msg to occur between hcf_service_nic and
 * hcf_rcv_msg.
 *
 *
 *.DIAGRAM
 *
 *.NOTICE
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
void
calc_mic_rx_frag( IFBP ifbp, wci_bufp p, int len )
{
	static union { hcf_32 x32; hcf_16 x16[2]; hcf_8 x8[4]; } x; //* area to accumulate 4 bytes input for MIC engine
	int i;

	if ( len == -1 ) {                              //initialize MIC housekeeping
		i = *(wci_recordp)&p[HFS_STAT];
		/* i = CNV_SHORTP_TO_LITTLE(&p[HFS_STAT]); should not be neede to prevent alignment poroblems
		 * since len == -1 if and only if p is lookahaead buffer which MUST be word aligned
		 * to be re-investigated by NvR
		 */

		if ( ( i & HFS_STAT_MIC ) == 0 ) {
			ifbp->IFB_MICRxCarry = 0xFFFF;          //suppress MIC calculation
		} else {
			ifbp->IFB_MICRxCarry = 0;
//* Note that "coincidentally" the bit positions used in HFS_STAT
//* correspond with the offset of the key in IFB_MICKey
			i = ( i & HFS_STAT_MIC_KEY_ID ) >> 10;  /* coincidentally no shift needed for i itself */
			ifbp->IFB_MICRx[0] = CNV_LONG_TO_LITTLE(ifbp->IFB_MICRxKey[i  ]);
			ifbp->IFB_MICRx[1] = CNV_LONG_TO_LITTLE(ifbp->IFB_MICRxKey[i+1]);
		}
	} else {
		if ( ifbp->IFB_MICRxCarry == 0 ) {
			x.x32 = CNV_LONGP_TO_LITTLE(p);
			p += 4;
			if ( len < 4 ) {
				ifbp->IFB_MICRxCarry = (hcf_16)len;
			} else {
				ifbp->IFB_MICRxCarry = 4;
				len -= 4;
			}
		} else while ( ifbp->IFB_MICRxCarry < 4 && len ) {      //note for hcf_16 applies: 0xFFFF > 4
				x.x8[ifbp->IFB_MICRxCarry++] = *p++;
				len--;
			}
		while ( ifbp->IFB_MICRxCarry == 4 ) {   //contrived so we have only 1 call to calc_mic so we could bring it in-line
			calc_mic( ifbp->IFB_MICRx, x.x32 );
			x.x32 = CNV_LONGP_TO_LITTLE(p);
			p += 4;
			if ( len < 4 ) {
				ifbp->IFB_MICRxCarry = (hcf_16)len;
			}
			len -= 4;
		}
	}
} // calc_mic_rx_frag
#endif // HCF_TYPE_WPA


#if (HCF_TYPE) & HCF_TYPE_WPA
/************************************************************************************************************
 *
 *.SUBMODULE     void calc_mic_tx_frag( IFBP ifbp, wci_bufp p, int len )
 *.PURPOSE       calculate MIC on a single fragment.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   bufp        (byte) address of buffer
 *   len         length in bytes of buffer specified by bufp
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * calc_mic_tx_frag ........
 *
 * The MIC is located in the IFB.
 * The MIC is separate for Tx and Rx, thus allowing hcf_send_msg to occur between hcf_service_nic and
 * hcf_rcv_msg.
 *
 *
 *.DIAGRAM
 *
 *.NOTICE
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
void
calc_mic_tx_frag( IFBP ifbp, wci_bufp p, int len )
{
	static union { hcf_32 x32; hcf_16 x16[2]; hcf_8 x8[4]; } x; //* area to accumulate 4 bytes input for MIC engine

	                                        //if initialization request
	if ( len == -1 ) {
		                                //.  presume MIC calculation disabled
		ifbp->IFB_MICTxCarry = 0xFFFF;
		                                //.  if MIC calculation enabled
		if ( ifbp->IFB_MICTxCntl ) {
			                        //.  .  clear MIC carry
			ifbp->IFB_MICTxCarry = 0;
			                        //.  .  initialize MIC-engine
			ifbp->IFB_MICTx[0] = CNV_LONG_TO_LITTLE(ifbp->IFB_MICTxKey[0]); /*Tx always uses Key 0 */
			ifbp->IFB_MICTx[1] = CNV_LONG_TO_LITTLE(ifbp->IFB_MICTxKey[1]);
		}
		                                //else
	} else {
		                                //.  if MIC enabled (Tx) / if MIC present (Rx)
		                                //.  and no carry from previous calc_mic_frag
		if ( ifbp->IFB_MICTxCarry == 0 ) {
			                        //.  .  preset accu with 4 bytes from buffer
			x.x32 = CNV_LONGP_TO_LITTLE(p);
			                        //.  .  adjust pointer accordingly
			p += 4;
			                        //.  .  if buffer contained less then 4 bytes
			if ( len < 4 ) {
				                //.  .  .  promote valid bytes in accu to carry
				                //.  .  .  flag accu to contain incomplete double word
				ifbp->IFB_MICTxCarry = (hcf_16)len;
				                //.  .  else
			} else {
				                //.  .  .  flag accu to contain complete double word
				ifbp->IFB_MICTxCarry = 4;
				                //.  .  adjust remaining buffer length
				len -= 4;
			}
			                        //.  else if MIC enabled
			                        //.  and if carry bytes from previous calc_mic_tx_frag
			                        //.  .  move (1-3) bytes from carry into accu
		} else while ( ifbp->IFB_MICTxCarry < 4 && len ) {      /* note for hcf_16 applies: 0xFFFF > 4 */
				x.x8[ifbp->IFB_MICTxCarry++] = *p++;
				len--;
			}
		                                //.  while accu contains complete double word
		                                //.  and MIC enabled
		while ( ifbp->IFB_MICTxCarry == 4 ) {
			                        //.  .  pass accu to MIC engine
			calc_mic( ifbp->IFB_MICTx, x.x32 );
			                        //.  .  copy next 4 bytes from buffer to accu
			x.x32 = CNV_LONGP_TO_LITTLE(p);
			                        //.  .  adjust buffer pointer
			p += 4;
			                        //.  .  if buffer contained less then 4 bytes
			                        //.  .  .  promote valid bytes in accu to carry
			                        //.  .  .  flag accu to contain incomplete double word
			if ( len < 4 ) {
				ifbp->IFB_MICTxCarry = (hcf_16)len;
			}
			                        //.  .  adjust remaining buffer length
			len -= 4;
		}
	}
} // calc_mic_tx_frag
#endif // HCF_TYPE_WPA


#if HCF_PROT_TIME
/************************************************************************************************************
 *
 *.SUBMODULE     void calibrate( IFBP ifbp )
 *.PURPOSE       calibrates the S/W protection counter against the Hermes Timer tick.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * calibrates the S/W protection counter against the Hermes Timer tick
 * IFB_TickIni is the value used to initialize the S/W protection counter such that the expiration period
 * more or less independent of the processor speed. If IFB_TickIni is not yet calibrated, it is done now.
 * This calibration is "reasonably" accurate because the Hermes is in a quiet state as a result of the
 * Initialize command.
 *
 *
 *.DIAGRAM
 *
 *1: IFB_TickIni is initialized at INI_TICK_INI by hcf_connect. If calibrate succeeds, IFB_TickIni is
 *   guaranteed to be changed. As a consequence there will be only 1 shot at calibration (regardless of the
 *   number of init calls) under normal circumstances.
 *2: Calibration is done HCF_PROT_TIME_CNT times. This diminish the effects of jitter and interference,
 *   especially in a pre-emptive environment. HCF_PROT_TIME_CNT is in the range of 16 through 32 and derived
 *   from the HCF_PROT_TIME specified by the MSF programmer. The divisor needed to scale HCF_PROT_TIME into the
 *   16-32 range, is used as a multiplicator after the calibration, to scale the found value back to the
 *   requested range. This way a compromise is achieved between accuracy and duration of the calibration
 *   process.
 *3: Acknowledge the Timer Tick Event.
 *   Each cycle is limited to at most INI_TICK_INI samples of the TimerTick status of the Hermes.
 *   Since the start of calibrate is unrelated to the Hermes Internal Timer, the first interval may last from 0
 *   to the normal interval, all subsequent intervals should be the full length of the Hermes Tick interval.
 *   The Hermes Timer Tick is not reprogrammed by the HCF, hence it is running at the default of 10 k
 *   microseconds.
 *4: If the Timer Tick Event is continuously up (prot_cnt still has the value INI_TICK_INI) or no Timer Tick
 *   Event occurred before the protection counter expired, reset IFB_TickIni to INI_TICK_INI,
 *   set the defunct bit of IFB_CardStat (thus rendering the Hermes inoperable) and exit the calibrate routine.
 *8: ifbp->IFB_TickIni is multiplied to scale the found value back to the requested range as explained under 2.
 *
 *.NOTICE
 * o Although there are a number of viewpoints possible, calibrate() uses as error strategy that a single
 *   failure of the Hermes TimerTick is considered fatal.
 * o There is no hard and concrete time-out value defined for Hermes activities. The default 1 seconds is
 *   believed to be sufficiently "relaxed" for real life and to be sufficiently short to be still useful in an
 *   environment with humans.
 * o Note that via IFB_DefunctStat time outs in cmd_wait and in hcfio_string block all Hermes access till the
 *   next init so functions which call a mix of cmd_wait and hcfio_string only need to check the return status
 *   of the last call
 * o The return code is preset at Time out.
 *   The additional complication that no calibrated value for the protection count can be assumed since
 *   calibrate() does not yet have determined a calibrated value (a catch 22), is handled by setting the
 *   initial value at INI_TICK_INI (by hcf_connect). This approach is considered safe, because:
 *     - the HCF does not use the pipeline mechanism of Hermes commands.
 *     - the likelihood of failure (the only time when protection count is relevant) is small.
 *     - the time will be sufficiently large on a fast machine (busy bit drops on good NIC before counter
 *       expires)
 *     - the time will be sufficiently small on a slow machine (counter expires on bad NIC before the end user
 *       switches the power off in despair
 *   The time needed to wrap a 32 bit counter around is longer than many humans want to wait, hence the more or
 *   less arbitrary value of 0x40000L is chosen, assuming it does not take too long on an XT and is not too
 *   short on a scream-machine.
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC void
calibrate( IFBP ifbp )
{
	int     cnt = HCF_PROT_TIME_CNT;
	hcf_32  prot_cnt;

	HCFTRACE( ifbp, HCF_TRACE_CALIBRATE );
	if ( ifbp->IFB_TickIni == INI_TICK_INI ) {                                                  /*1*/
		ifbp->IFB_TickIni = 0;                                                                  /*2*/
		while ( cnt-- ) {
			prot_cnt = INI_TICK_INI;
			OPW( HREG_EV_ACK, HREG_EV_TICK );                                               /*3*/
			while ( (IPW( HREG_EV_STAT ) & HREG_EV_TICK) == 0 && --prot_cnt ) {
				ifbp->IFB_TickIni++;
			}
			if ( prot_cnt == 0 || prot_cnt == INI_TICK_INI ) {                              /*4*/
				ifbp->IFB_TickIni = INI_TICK_INI;
				ifbp->IFB_DefunctStat = HCF_ERR_DEFUNCT_TIMER;
				ifbp->IFB_CardStat |= CARD_STAT_DEFUNCT;
				HCFASSERT( DO_ASSERT, prot_cnt );
			}
		}
		ifbp->IFB_TickIni <<= HCF_PROT_TIME_SHFT;                                               /*8*/
	}
	HCFTRACE( ifbp, HCF_TRACE_CALIBRATE | HCF_TRACE_EXIT );
} // calibrate
#endif // HCF_PROT_TIME


#if (HCF_TYPE) & HCF_TYPE_WPA
/************************************************************************************************************
 *
 *.SUBMODULE     int check_mic( IFBP ifbp )
 *.PURPOSE       verifies the MIC of a received non-USB frame.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS
 *   HCF_SUCCESS
 *   HCF_ERR_MIC
 *
 *.DESCRIPTION
 *
 *
 *.DIAGRAM
 *
 *4: test whether or not a MIC is reported by the Hermes
 *14: the calculated MIC and the received MIC are compared, the return status is set when there is a mismatch
 *
 *.NOTICE
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
static int
check_mic( IFBP ifbp )
{
	int     rc = HCF_SUCCESS;
	hcf_32 x32[2];              //* area to save rcvd 8 bytes MIC

	                                                    //if MIC present in RxFS
	if ( *(wci_recordp)&ifbp->IFB_lap[-HFS_ADDR_DEST] & HFS_STAT_MIC ) {
		//or if ( ifbp->IFB_MICRxCarry != 0xFFFF )
		CALC_RX_MIC( mic_pad, 8 );                  //.  process up to 3 remaining bytes of data and append 5 to 8 bytes of padding to MIC calculation
		get_frag( ifbp, (wci_bufp)x32, 8 BE_PAR(0));//.  get 8 byte MIC from NIC
		                                            //.  if calculated and received MIC do not match
		                                            //.  .  set status at HCF_ERR_MIC
	/*14*/  if ( x32[0] != CNV_LITTLE_TO_LONG(ifbp->IFB_MICRx[0]) ||
		     x32[1] != CNV_LITTLE_TO_LONG(ifbp->IFB_MICRx[1])     ) {
			rc = HCF_ERR_MIC;
		}
	}
	                                                    //return status
	return rc;
} // check_mic
#endif // HCF_TYPE_WPA


/************************************************************************************************************
 *
 *.SUBMODULE     int cmd_cmpl( IFBP ifbp )
 *.PURPOSE       waits for Hermes Command Completion.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS
 *   IFB_DefunctStat
 *   HCF_ERR_TIME_OUT
 *   HCF_ERR_DEFUNCT_CMD_SEQ
 *   HCF_SUCCESS
 *
 *.DESCRIPTION
 *
 *
 *.DIAGRAM
 *
 *2: Once cmd_cmpl is called, the Busy option bit in IFB_Cmd must be cleared
 *4: If Status register and command code don't match either:
 *    - the Hermes and Host are out of sync ( a fatal error)
 *    - error bits are reported via the Status Register.
 *   Out of sync is considered fatal and brings the HCF in Defunct mode
 *   Errors reported via the Status Register should be caused by sequence violations in Hermes command
 *   sequences and hence these bugs should have been found during engineering testing. Since there is no
 *   strategy to cope with this problem, it might as well be ignored at run time. Note that for any particular
 *   situation where a strategy is formulated to handle the consequences of a particular bug causing a
 *   particular Error situation reported via the Status Register, the bug should be removed rather than adding
 *   logic to cope with the consequences of the bug.
 *   There have been HCF versions where an error report via the Status Register even brought the HCF in defunct
 *   mode (although it was not yet named like that at that time). This is particular undesirable behavior for a
 *   general library.
 *   Simply reporting the error (as "interesting") is debatable. There also have been HCF versions with this
 *   strategy using the "vague" HCF_FAILURE code.
 *   The error is reported via:
 *    - MiscErr tally of the HCF Tally set
 *    - the (informative) fields IFB_ErrCmd and IFB_ErrQualifier
 *    - the assert mechanism
 *8: Here the Defunct case and the Status error are separately treated
 *
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC int
cmd_cmpl( IFBP ifbp )
{

	PROT_CNT_INI;
	int     rc = HCF_SUCCESS;
	hcf_16  stat;

	HCFLOGENTRY( HCF_TRACE_CMD_CPL, ifbp->IFB_Cmd );
	ifbp->IFB_Cmd &= ~HCMD_BUSY;                                                /* 2 */
	HCF_WAIT_WHILE( (IPW( HREG_EV_STAT) & HREG_EV_CMD) == 0 );                  /* 4 */
	stat = IPW( HREG_STAT );
#if HCF_PROT_TIME
	if ( prot_cnt == 0 ) {
		IF_TALLY( ifbp->IFB_HCF_Tallies.MiscErr++ );
		rc = HCF_ERR_TIME_OUT;
		HCFASSERT( DO_ASSERT, ifbp->IFB_Cmd );
	} else
#endif // HCF_PROT_TIME
	{
		DAWA_ACK( HREG_EV_CMD );
	/*4*/   if ( stat != (ifbp->IFB_Cmd & HCMD_CMD_CODE) ) {
		/*8*/   if ( ( (stat ^ ifbp->IFB_Cmd ) & HCMD_CMD_CODE) != 0 ) {
				rc = ifbp->IFB_DefunctStat = HCF_ERR_DEFUNCT_CMD_SEQ;
				ifbp->IFB_CardStat |= CARD_STAT_DEFUNCT;
			}
			IF_TALLY( ifbp->IFB_HCF_Tallies.MiscErr++ );
			ifbp->IFB_ErrCmd = stat;
			ifbp->IFB_ErrQualifier = IPW( HREG_RESP_0 );
			HCFASSERT( DO_ASSERT, MERGE_2( IPW( HREG_PARAM_0 ), ifbp->IFB_Cmd ) );
			HCFASSERT( DO_ASSERT, MERGE_2( ifbp->IFB_ErrQualifier, ifbp->IFB_ErrCmd ) );
		}
	}
	HCFASSERT( rc == HCF_SUCCESS, rc);
	HCFLOGEXIT( HCF_TRACE_CMD_CPL );
	return rc;
} // cmd_cmpl


/************************************************************************************************************
 *
 *.SUBMODULE     int cmd_exe( IFBP ifbp, int cmd_code, int par_0 )
 *.PURPOSE       Executes synchronous part of Hermes Command and - optionally - waits for Command Completion.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   cmd_code
 *   par_0
 *
 *.RETURNS
 *   IFB_DefunctStat
 *   HCF_ERR_DEFUNCT_CMD_SEQ
 *   HCF_SUCCESS
 *   HCF_ERR_TO_BE_ADDED <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 *
 *.DESCRIPTION
 * Executes synchronous Hermes Command and waits for Command Completion
 *
 * The general HCF strategy is to wait for command completion. As a consequence:
 * - the read of the busy bit before writing the command register is superfluous
 * - the Hermes requirement that no Inquiry command may be executed if there is still an unacknowledged
 *   Inquiry command outstanding, is automatically met.
 * The Tx command uses the "Busy" bit in the cmd_code parameter to deviate from this general HCF strategy.
 * The idea is that by not busy-waiting on completion of this frequently used command the processor
 * utilization is diminished while using the busy-wait on all other seldom used commands the flow is kept
 * simple.
 *
 *
 *
 *.DIAGRAM
 *
 *1: skip the body of cmd_exe when in defunct mode or when - based on the S/W Support register write and
 *   read back test - there is apparently no NIC.
 *   Note: we gave up on the "old" strategy to write the S/W Support register at magic only when needed. Due to
 *   the intricateness of Hermes F/W varieties ( which behave differently as far as corruption of the S/W
 *   Support register is involved), the increasing number of Hermes commands which do an implicit initialize
 *   (thus modifying the S/W Support register) and the workarounds of some OS/Support S/W induced aspects (e.g.
 *   the System Soft library at WinNT which postpones the actual mapping of I/O space up to 30 seconds after
 *   giving the go-ahead), the "magic" strategy is now reduced to a simple write and read back. This means that
 *   problems like a bug tramping over the memory mapped Hermes registers will no longer be noticed as side
 *   effect of the S/W Support register check.
 *2: check whether the preceding command skipped the busy wait and if so, check for command completion
 *
 *.NOTICE
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/

HCF_STATIC int
cmd_exe( IFBP ifbp, hcf_16 cmd_code, hcf_16 par_0 ) //if HCMD_BUSY of cmd_code set, then do NOT wait for completion
{
	int rc;

	HCFLOGENTRY( HCF_TRACE_CMD_EXE, cmd_code );
	HCFASSERT( (cmd_code & HCMD_CMD_CODE) != HCMD_TX || cmd_code & HCMD_BUSY, cmd_code ); //Tx must have Busy bit set
	OPW( HREG_SW_0, HCF_MAGIC );
	if ( IPW( HREG_SW_0 ) == HCF_MAGIC ) {                                                      /* 1 */
		rc = ifbp->IFB_DefunctStat;
	}
	else rc = HCF_ERR_NO_NIC;
	if ( rc == HCF_SUCCESS ) {
		//;?is this a hot idea, better MEASURE performance impact
	/*2*/   if ( ifbp->IFB_Cmd & HCMD_BUSY ) {
			rc = cmd_cmpl( ifbp );
		}
		OPW( HREG_PARAM_0, par_0 );
		OPW( HREG_CMD, cmd_code &~HCMD_BUSY );
		ifbp->IFB_Cmd = cmd_code;
		if ( (cmd_code & HCMD_BUSY) == 0 ) {    //;?is this a hot idea, better MEASURE performance impact
			rc = cmd_cmpl( ifbp );
		}
	}
	HCFASSERT( rc == HCF_SUCCESS, MERGE_2( rc, cmd_code ) );
	HCFLOGEXIT( HCF_TRACE_CMD_EXE );
	return rc;
} // cmd_exe


/************************************************************************************************************
 *
 *.SUBMODULE     int download( IFBP ifbp, CFG_PROG_STRCT FAR *ltvp )
 *.PURPOSE       downloads F/W image into NIC and initiates execution of the downloaded F/W.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   ltvp        specifies the pseudo-RID (as defined by WCI)
 *
 *.RETURNS
 *
 *.DESCRIPTION
 *
 *
 *.DIAGRAM
 *1: First, Ack everything to unblock a (possibly) blocked cmd pipe line
 *   Note 1: it is very likely that an Alloc event is pending and very well possible that a (Send) Cmd event is
 *   pending
 *   Note 2: it is assumed that this strategy takes away the need to ack every conceivable event after an
 *   Hermes Initialize
 *
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC int
download( IFBP ifbp, CFG_PROG_STRCT FAR *ltvp )                     //Hermes-II download (volatile only)
{
	hcf_16              i;
	int                 rc = HCF_SUCCESS;
	wci_bufp            cp;
	hcf_io              io_port = ifbp->IFB_IOBase + HREG_AUX_DATA;

	HCFLOGENTRY( HCF_TRACE_DL, ltvp->typ );
#if (HCF_TYPE) & HCF_TYPE_PRELOADED
	HCFASSERT( DO_ASSERT, ltvp->mode );
#else
	                                        //if initial "program" LTV
	if ( ifbp->IFB_DLMode == CFG_PROG_STOP && ltvp->mode == CFG_PROG_VOLATILE) {
		                                //.  switch Hermes to initial mode
	/*1*/   OPW( HREG_EV_ACK, ~HREG_EV_SLEEP_REQ );
		rc = cmd_exe( ifbp, HCMD_INI, 0 );  /* HCMD_INI can not be part of init() because that is called on
						     * other occasions as well */
		rc = init( ifbp );
	}
	                                        //if final "program" LTV
	if ( ltvp->mode == CFG_PROG_STOP && ifbp->IFB_DLMode == CFG_PROG_VOLATILE) {
		                                //.  start tertiary (or secondary)
		OPW( HREG_PARAM_1, (hcf_16)(ltvp->nic_addr >> 16) );
		rc = cmd_exe( ifbp, HCMD_EXECUTE, (hcf_16) ltvp->nic_addr );
		if (rc == HCF_SUCCESS) {
			rc = init( ifbp );  /*;? do we really want to skip init if cmd_exe failed, i.e.
					     *   IFB_FW_Comp_Id is than possibly incorrect */
		}
		                                //else (non-final)
	} else {
		                                //.  if mode == Readback SEEPROM
#if 0   //;? as long as the next if contains a hard coded 0, might as well leave it out even more obvious
		if ( 0 /*len is definitely not want we want;?*/ && ltvp->mode == CFG_PROG_SEEPROM_READBACK ) {
			OPW( HREG_PARAM_1, (hcf_16)(ltvp->nic_addr >> 16) );
			OPW( HREG_PARAM_2, (hcf_16)((ltvp->len - 4) << 1) );
			                        //.  .  perform Hermes prog cmd with appropriate mode bits
			rc = cmd_exe( ifbp, HCMD_PROGRAM | ltvp->mode, (hcf_16)ltvp->nic_addr );
			                        //.  .  set up NIC RAM addressability according Resp0-1
			OPW( HREG_AUX_PAGE,   IPW( HREG_RESP_1) );
			OPW( HREG_AUX_OFFSET, IPW( HREG_RESP_0) );
			                        //.  .  set up L-field of LTV according Resp2
			i = ( IPW( HREG_RESP_2 ) + 1 ) / 2;  // i contains max buffer size in words, a probably not very useful piece of information ;?
/*Nico's code based on i is the "real amount of data available"
			if ( ltvp->len - 4 < i ) rc = HCF_ERR_LEN;
			else ltvp->len = i + 4;
*/
/* Rolands code based on the idea that a MSF should not ask for more than is available
			// check if number of bytes requested exceeds max buffer size
			if ( ltvp->len - 4 > i ) {
				rc = HCF_ERR_LEN;
				ltvp->len = i + 4;
			}
*/
			                        //.  .  copy data from NIC via AUX port to LTV
			cp = (wci_bufp)ltvp->host_addr;                     /*IN_PORT_STRING_8_16 macro may modify its parameters*/
			i = ltvp->len - 4;
			IN_PORT_STRING_8_16( io_port, cp, i );      //!!!WORD length, cp MUST be a char pointer // $$ char
			                        //.  else (non-final programming)
		} else
#endif //;? as long as the above if contains a hard coded 0, might as well leave it out even more obvious
		{                               //.  .  get number of words to program
			HCFASSERT( ltvp->segment_size, *ltvp->host_addr );
			i = ltvp->segment_size/2;
			                        //.  .  copy data (words) from LTV via AUX port to NIC
			cp = (wci_bufp)ltvp->host_addr;                     //OUT_PORT_STRING_8_16 macro may modify its parameters
			                        //.  .  if mode == volatile programming
			if ( ltvp->mode == CFG_PROG_VOLATILE ) {
				                //.  .  .  set up NIC RAM addressability via AUX port
				OPW( HREG_AUX_PAGE, (hcf_16)(ltvp->nic_addr >> 16 << 9 | (ltvp->nic_addr & 0xFFFF) >> 7 ) );
				OPW( HREG_AUX_OFFSET, (hcf_16)(ltvp->nic_addr & 0x007E) );
				OUT_PORT_STRING_8_16( io_port, cp, i );     //!!!WORD length, cp MUST be a char pointer
			}
		}
	}
	ifbp->IFB_DLMode = ltvp->mode;                  //save state in IFB_DLMode
#endif // HCF_TYPE_PRELOADED
	HCFASSERT( rc == HCF_SUCCESS, rc );
	HCFLOGEXIT( HCF_TRACE_DL );
	return rc;
} // download


#if (HCF_ASSERT) & HCF_ASSERT_PRINTF
/**************************************************
 * Certain Hermes-II firmware versions can generate
 * debug information. This debug information is
 * contained in a buffer in nic-RAM, and can be read
 * via the aux port.
 **************************************************/
HCF_STATIC int
fw_printf(IFBP ifbp, CFG_FW_PRINTF_STRCT FAR *ltvp)
{
	int rc = HCF_SUCCESS;
	hcf_16 fw_cnt;
//	hcf_32 DbMsgBuffer = 0x29D2, DbMsgCount= 0x000029D0;
//	hcf_16 DbMsgSize=0x00000080;
	hcf_32 DbMsgBuffer;
	CFG_FW_PRINTF_BUFFER_LOCATION_STRCT *p = &ifbp->IFB_FwPfBuff;
	ltvp->len = 1;
	if ( p->DbMsgSize != 0 ) {
		// first, check the counter in nic-RAM and compare it to the latest counter value of the HCF
		OPW( HREG_AUX_PAGE, (hcf_16)(p->DbMsgCount >> 7) );
		OPW( HREG_AUX_OFFSET, (hcf_16)(p->DbMsgCount & 0x7E) );
		fw_cnt = ((IPW( HREG_AUX_DATA) >>1 ) & ((hcf_16)p->DbMsgSize - 1));
		if ( fw_cnt != ifbp->IFB_DbgPrintF_Cnt ) {
//			DbgPrint("fw_cnt=%d IFB_DbgPrintF_Cnt=%d\n", fw_cnt, ifbp->IFB_DbgPrintF_Cnt);
			DbMsgBuffer = p->DbMsgBuffer + ifbp->IFB_DbgPrintF_Cnt * 6; // each entry is 3 words
			OPW( HREG_AUX_PAGE, (hcf_16)(DbMsgBuffer >> 7) );
			OPW( HREG_AUX_OFFSET, (hcf_16)(DbMsgBuffer & 0x7E) );
			ltvp->msg_id     = IPW(HREG_AUX_DATA);
			ltvp->msg_par    = IPW(HREG_AUX_DATA);
			ltvp->msg_tstamp = IPW(HREG_AUX_DATA);
			ltvp->len = 4;
			ifbp->IFB_DbgPrintF_Cnt++;
			ifbp->IFB_DbgPrintF_Cnt &= (p->DbMsgSize - 1);
		}
	}
	return rc;
};
#endif // HCF_ASSERT_PRINTF


/************************************************************************************************************
 *
 *.SUBMODULE     hcf_16 get_fid( IFBP ifbp )
 *.PURPOSE       get allocated FID for either transmit or notify.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS
 *   0   no FID available
 *   <>0 FID number
 *
 *.DESCRIPTION
 *
 *
 *.DIAGRAM
 *   The preference is to use a "pending" alloc. If no alloc is pending, then - if available - the "spare" FID
 *   is used.
 *   If the spare FID is used, IFB_RscInd (representing the spare FID) must be cleared
 *   If the pending alloc is used, the alloc event must be acknowledged to the Hermes.
 *   In case the spare FID was depleted and the IFB_RscInd has been "faked" as pseudo resource with a 0x0001
 *   value by hcf_service_nic, IFB_RscInd has to be "corrected" again to its 0x0000 value.
 *
 *   Note that due to the Hermes-II H/W problems which are intended to be worked around by DAWA, the Alloc bit
 *   in the Event register is no longer a reliable indication of the presence/absence of a FID. The "Clear FID"
 *   part of the DAWA logic, together with the choice of the definition of the return information from get_fid,
 *   handle this automatically, i.e. without additional code in get_fid.
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC hcf_16
get_fid( IFBP ifbp )
{

	hcf_16 fid = 0;
#if ( (HCF_TYPE) & HCF_TYPE_HII5 ) == 0
	PROT_CNT_INI;
#endif // HCF_TYPE_HII5

	IF_DMA( HCFASSERT(!(ifbp->IFB_CntlOpt & USE_DMA), ifbp->IFB_CntlOpt) );

	if ( IPW( HREG_EV_STAT) & HREG_EV_ALLOC) {
		fid = IPW( HREG_ALLOC_FID );
		HCFASSERT( fid, ifbp->IFB_RscInd );
		DAWA_ZERO_FID( HREG_ALLOC_FID );
#if ( (HCF_TYPE) & HCF_TYPE_HII5 ) == 0
		HCF_WAIT_WHILE( ( IPW( HREG_EV_STAT ) & HREG_EV_ACK_REG_READY ) == 0 );
		HCFASSERT( prot_cnt, IPW( HREG_EV_STAT ) );
#endif // HCF_TYPE_HII5
		DAWA_ACK( HREG_EV_ALLOC );          //!!note that HREG_EV_ALLOC is written only once
// 180 degree error in logic ;? #if ALLOC_15
		if ( ifbp->IFB_RscInd == 1 ) {
			ifbp->IFB_RscInd = 0;
		}
//#endif // ALLOC_15
	} else {
// 180 degree error in logic ;? #if ALLOC_15
		fid = ifbp->IFB_RscInd;
//#endif // ALLOC_15
		ifbp->IFB_RscInd = 0;
	}
	return fid;
} // get_fid


/************************************************************************************************************
 *
 *.SUBMODULE     void get_frag( IFBP ifbp, wci_bufp bufp, int len BE_PAR( int word_len ) )
 *.PURPOSE       reads with 16/32 bit I/O via BAP1 port from NIC RAM to Host memory.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   bufp        (byte) address of buffer
 *   len         length in bytes of buffer specified by bufp
 *   word_len    Big Endian only: number of leading bytes to swap in pairs
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * process the single byte (if applicable) read by the previous get_frag and copy len (or len-1) bytes from
 * NIC to bufp.
 * On a Big Endian platform, the parameter word_len controls the number of leading bytes whose endianness is
 * converted (i.e. byte swapped)
 *
 *
 *.DIAGRAM
 *10: The PCMCIA card can be removed in the middle of the transfer. By depositing a "magic number" in the
 *   HREG_SW_0 register of the Hermes at initialization time and by verifying this register, it can be
 *   determined whether the card is still present. The return status is set accordingly.
 *   Clearing the buffer is a (relative) cheap way to prevent that failing I/O results in run-away behavior
 *   because the garbage in the buffer is interpreted by the caller irrespective of the return status (e.g.
 *   hcf_service_nic has this behavior).
 *
 *.NOTICE
 *   It turns out DOS ODI uses zero length fragments. The HCF code can cope with it, but as a consequence, no
 *   Assert on len is possible
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC void
get_frag( IFBP ifbp, wci_bufp bufp, int len BE_PAR( int word_len ) )
{
	hcf_io      io_port = ifbp->IFB_IOBase + HREG_DATA_1;   //BAP data register
	wci_bufp    p = bufp;                                   //working pointer
	int         i;                                          //prevent side effects from macro
	int         j;

	HCFASSERT( ((hcf_32)bufp & (HCF_ALIGN-1) ) == 0, (hcf_32)bufp );

/*1:    here recovery logic for intervening BAP access between hcf_service_nic and hcf_rcv_msg COULD be added
 *  if current access is RxInitial
 *  .  persistent_offset += len
 */

	i = len;
	                                        //if buffer length > 0 and carry from previous get_frag
	if ( i && ifbp->IFB_CarryIn ) {
		                                //.  move carry to buffer
		                                //.  adjust buffer length and pointer accordingly
		*p++ = (hcf_8)(ifbp->IFB_CarryIn>>8);
		i--;
		                                //.  clear carry flag
		ifbp->IFB_CarryIn = 0;
	}
#if (HCF_IO) & HCF_IO_32BITS
	//skip zero-length I/O, single byte I/O and I/O not worthwhile (i.e. less than 6 bytes)for DW logic
	                                        //if buffer length >= 6 and 32 bits I/O support
	if ( !(ifbp->IFB_CntlOpt & USE_16BIT) && i >= 6 ) {
		hcf_32 FAR  *p4; //prevent side effects from macro
		if ( ( (hcf_32)p & 0x1 ) == 0 ) {           //.  if buffer at least word aligned
			if ( (hcf_32)p & 0x2 ) {            //.  .  if buffer not double word aligned
							    //.  .  .  read single word to get double word aligned
				*(wci_recordp)p = IN_PORT_WORD( io_port );
				                            //.  .  .  adjust buffer length and pointer accordingly
				p += 2;
				i -= 2;
			}
			                                    //.  .  read as many double word as possible
			p4 = (hcf_32 FAR *)p;
			j = i/4;
			IN_PORT_STRING_32( io_port, p4, j );
			                                    //.  .  adjust buffer length and pointer accordingly
			p += i & ~0x0003;
			i &= 0x0003;
		}
	}
#endif // HCF_IO_32BITS
	                                        //if no 32-bit support OR byte aligned OR 1-3 bytes left
	if ( i ) {
		                                //.  read as many word as possible in "alignment safe" way
		j = i/2;
		IN_PORT_STRING_8_16( io_port, p, j );
		                                //.  if 1 byte left
		if ( i & 0x0001 ) {
			                        //.  .  read 1 word
			ifbp->IFB_CarryIn = IN_PORT_WORD( io_port );
			                        //.  .  store LSB in last char of buffer
			bufp[len-1] = (hcf_8)ifbp->IFB_CarryIn;
			                        //.  .  save MSB in carry, set carry flag
			ifbp->IFB_CarryIn |= 0x1;
		}
	}
#if HCF_BIG_ENDIAN
	HCFASSERT( word_len == 0 || word_len == 2 || word_len == 4, word_len );
	HCFASSERT( word_len == 0 || ((hcf_32)bufp & 1 ) == 0, (hcf_32)bufp );
	HCFASSERT( word_len <= len, MERGE2( word_len, len ) );
	//see put_frag for an alternative implementation, but be careful about what are int's and what are
	//hcf_16's
	if ( word_len ) {                               //.  if there is anything to convert
		hcf_8 c;
		c = bufp[1];                                //.  .  convert the 1st hcf_16
		bufp[1] = bufp[0];
		bufp[0] = c;
		if ( word_len > 1 ) {                       //.  .  if there is to convert more than 1 word ( i.e 2 )
			c = bufp[3];                            //.  .  .  convert the 2nd hcf_16
			bufp[3] = bufp[2];
			bufp[2] = c;
		}
	}
#endif // HCF_BIG_ENDIAN
} // get_frag

/************************************************************************************************************
 *
 *.SUBMODULE     int init( IFBP ifbp )
 *.PURPOSE       Handles common initialization aspects (H-I init, calibration, config.mngmt, allocation).
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS
 *   HCF_ERR_INCOMP_PRI
 *   HCF_ERR_INCOMP_FW
 *   HCF_ERR_TIME_OUT
 *   >>hcf_get_info
 *       HCF_ERR_NO_NIC
 *       HCF_ERR_LEN
 *
 *.DESCRIPTION
 * init will successively:
 * - in case of a (non-preloaded) H-I, initialize the NIC
 * - calibrate the S/W protection timer against the Hermes Timer
 * - collect HSI, "active" F/W Configuration Management Information
 * - in case active F/W is Primary F/W: collect Primary F/W Configuration Management Information
 * - check HSI and Primary F/W compatibility with the HCF
 * - in case active F/W is Station or AP F/W: check Station or AP F/W compatibility with the HCF
 * - in case active F/W is not Primary F/W: allocate FIDs to be used in transmit/notify process
 *
 *
 *.DIAGRAM
 *2: drop all error status bits in IFB_CardStat since they are expected to be re-evaluated.
 *4: Ack everything except HREG_EV_SLEEP_REQ. It is very likely that an Alloc event is pending and
 *   very well possible that a Send Cmd event is pending. Acking HREG_EV_SLEEP_REQ is handled by hcf_action(
 *   HCF_ACT_INT_ON ) !!!
 *10: Calibrate the S/W time-out protection mechanism by calling calibrate(). Note that possible errors
 *   in the calibration process are nor reported by init but will show up via the defunct mechanism in
 *   subsequent hcf-calls.
 *14: usb_check_comp() is called to have the minimal visual clutter for the legacy H-I USB dongle
 *   compatibility check.
 *16: The following configuration management related information is retrieved from the NIC:
 *    - HSI supplier
 *    - F/W Identity
 *    - F/W supplier
 *    if appropriate:
 *    - PRI Identity
 *    - PRI supplier
 *    appropriate means on H-I: always
 *    and on H-II if F/W supplier reflects a primary (i.e. only after an Hermes Reset or Init
 *    command).
 *    QUESTION ;? !!!!!! should, For each of the above RIDs the Endianness is converted to native Endianness.
 *    Only the return code of the first hcf_get_info is used. All hcf_get_info calls are made, regardless of
 *    the success or failure of the 1st hcf_get_info. The assumptions are:
 *     - if any call fails, they all fail, so remembering the result of the 1st call is adequate
 *     - a failing call will overwrite the L-field with a 0x0000 value, which services both as an
 *       error indication for the values cached in the IFB as making mmd_check_comp fail.
 *    In case of H-I, when getting the F/W identity fails, the F/W is assumed to be H-I AP F/W pre-dating
 *    version 9.0 and the F/W Identity and Supplier are faked accordingly.
 *    In case of H-II, the Primary, Station and AP Identity are merged into a single F/W Identity.
 *    The same applies to the Supplier information. As a consequence the PRI information can no longer be
 *    retrieved when a Tertiary runs. To accommodate MSFs and Utilities who depend on PRI information being
 *    available at any time, this information is cached in the IFB. In this cache the generic "F/W" value of
 *    the typ-fields is overwritten with the specific (legacy) "PRI" values. To actually re-route the (legacy)
 *    PRI request via hcf_get_info, the xxxx-table must be set.  In case of H-I, this caching, modifying and
 *    re-routing is not needed because PRI information is always available directly from the NIC. For
 *    consistency the caching fields in the IFB are filled with the PRI information anyway.
 *18: mdd_check_comp() is called to check the Supplier Variant and Range of the Host-S/W I/F (HSI) and the
 *   Primary Firmware Variant and Range against the Top and Bottom level supported by this HCF.  If either of
 *   these tests fails, the CARD_STAT_INCOMP_PRI bit of IFB_CardStat is set
 *   Note: There should always be a primary except during production, so this makes the HCF in its current form
 *   unsuitable for manufacturing test systems like the FTS. This can be remedied by an adding a test like
 *   ifbp->IFB_PRISup.id == COMP_ID_PRI
 *20: In case there is Tertiary F/W and this F/W is Station F/W, the Supplier Variant and Range of the Station
 *   Firmware function as retrieved from the Hermes is checked against the Top and Bottom level supported by
 *   this HCF.
 *   Note: ;? the tertiary F/W compatibility checks could be moved to the DHF, which already has checked the
 *   CFI and MFI compatibility of the image with the NIC before the image was downloaded.
 *28: In case of non-Primary F/W: allocates and acknowledge a (TX or Notify) FID and allocates without
 *   acknowledge another (TX or Notify) FID (the so-called 1.5 alloc scheme) with the following steps:
 *   - execute the allocate command by calling cmd_exe
 *   - wait till either the alloc event or a time-out occurs
 *   - regardless whether the alloc event occurs, call get_fid to
 *     - read the FID and save it in IFB_RscInd to be used as "spare FID"
 *     - acknowledge the alloc event
 *     - do another "half" allocate to complete the "1.5 Alloc scheme"
 *     Note that above 3 steps do not harm and thus give the "cheapest" acceptable strategy.
 *     If a time-out occurred, then report time out status (after all)
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC int
init( IFBP ifbp )
{

	int rc = HCF_SUCCESS;

	HCFLOGENTRY( HCF_TRACE_INIT, 0 );

	ifbp->IFB_CardStat = 0;                                                                         /* 2*/
	OPW( HREG_EV_ACK, ~HREG_EV_SLEEP_REQ );                                             /* 4*/
	IF_PROT_TIME( calibrate( ifbp ) );                                                  /*10*/
#if 0 // OOR
	ifbp->IFB_FWIdentity.len = 2;                           //misuse the IFB space for a put
	ifbp->IFB_FWIdentity.typ = CFG_TICK_TIME;
	ifbp->IFB_FWIdentity.comp_id = (1000*1000)/1024 + 1;    //roughly 1 second
	hcf_put_info( ifbp, (LTVP)&ifbp->IFB_FWIdentity.len );
#endif // OOR
	ifbp->IFB_FWIdentity.len = sizeof(CFG_FW_IDENTITY_STRCT)/sizeof(hcf_16) - 1;
	ifbp->IFB_FWIdentity.typ = CFG_FW_IDENTITY;
	rc = hcf_get_info( ifbp, (LTVP)&ifbp->IFB_FWIdentity.len );
/* ;? conversion should not be needed for mmd_check_comp */
#if HCF_BIG_ENDIAN
	ifbp->IFB_FWIdentity.comp_id       = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWIdentity.comp_id );
	ifbp->IFB_FWIdentity.variant       = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWIdentity.variant );
	ifbp->IFB_FWIdentity.version_major = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWIdentity.version_major );
	ifbp->IFB_FWIdentity.version_minor = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWIdentity.version_minor );
#endif // HCF_BIG_ENDIAN
#if defined MSF_COMPONENT_ID                                                                        /*14*/
	if ( rc == HCF_SUCCESS ) {                                                                      /*16*/
		ifbp->IFB_HSISup.len = sizeof(CFG_SUP_RANGE_STRCT)/sizeof(hcf_16) - 1;
		ifbp->IFB_HSISup.typ = CFG_NIC_HSI_SUP_RANGE;
		rc = hcf_get_info( ifbp, (LTVP)&ifbp->IFB_HSISup.len );
/* ;? conversion should not be needed for mmd_check_comp , BUT according to a report of a BE-user it is
 * should be resolved in the WARP release
 * since some compilers make ugly but unnecessary code of these instructions even for LE,
 * it is conditionally compiled */
#if HCF_BIG_ENDIAN
		ifbp->IFB_HSISup.role    = CNV_LITTLE_TO_SHORT( ifbp->IFB_HSISup.role );
		ifbp->IFB_HSISup.id      = CNV_LITTLE_TO_SHORT( ifbp->IFB_HSISup.id );
		ifbp->IFB_HSISup.variant = CNV_LITTLE_TO_SHORT( ifbp->IFB_HSISup.variant );
		ifbp->IFB_HSISup.bottom  = CNV_LITTLE_TO_SHORT( ifbp->IFB_HSISup.bottom );
		ifbp->IFB_HSISup.top     = CNV_LITTLE_TO_SHORT( ifbp->IFB_HSISup.top );
#endif // HCF_BIG_ENDIAN
		ifbp->IFB_FWSup.len = sizeof(CFG_SUP_RANGE_STRCT)/sizeof(hcf_16) - 1;
		ifbp->IFB_FWSup.typ = CFG_FW_SUP_RANGE;
		(void)hcf_get_info( ifbp, (LTVP)&ifbp->IFB_FWSup.len );
/* ;? conversion should not be needed for mmd_check_comp */
#if HCF_BIG_ENDIAN
		ifbp->IFB_FWSup.role    = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWSup.role );
		ifbp->IFB_FWSup.id      = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWSup.id );
		ifbp->IFB_FWSup.variant = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWSup.variant );
		ifbp->IFB_FWSup.bottom  = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWSup.bottom );
		ifbp->IFB_FWSup.top     = CNV_LITTLE_TO_SHORT( ifbp->IFB_FWSup.top );
#endif // HCF_BIG_ENDIAN

		if ( ifbp->IFB_FWSup.id == COMP_ID_PRI ) {                                              /* 20*/
			int i = sizeof( CFG_FW_IDENTITY_STRCT) + sizeof(CFG_SUP_RANGE_STRCT );
			while ( i-- ) ((hcf_8*)(&ifbp->IFB_PRIIdentity))[i] = ((hcf_8*)(&ifbp->IFB_FWIdentity))[i];
			ifbp->IFB_PRIIdentity.typ = CFG_PRI_IDENTITY;
			ifbp->IFB_PRISup.typ = CFG_PRI_SUP_RANGE;
			xxxx[xxxx_PRI_IDENTITY_OFFSET] = &ifbp->IFB_PRIIdentity.len;
			xxxx[xxxx_PRI_IDENTITY_OFFSET+1] = &ifbp->IFB_PRISup.len;
		}
		if ( !mmd_check_comp( (void*)&cfg_drv_act_ranges_hsi, &ifbp->IFB_HSISup)                 /* 22*/
#if ( (HCF_TYPE) & HCF_TYPE_PRELOADED ) == 0
//;? the PRI compatibility check is only relevant for DHF
		     || !mmd_check_comp( (void*)&cfg_drv_act_ranges_pri, &ifbp->IFB_PRISup)
#endif // HCF_TYPE_PRELOADED
			) {
			ifbp->IFB_CardStat = CARD_STAT_INCOMP_PRI;
			rc = HCF_ERR_INCOMP_PRI;
		}
		if ( ( ifbp->IFB_FWSup.id == COMP_ID_STA && !mmd_check_comp( (void*)&cfg_drv_act_ranges_sta, &ifbp->IFB_FWSup) ) ||
		     ( ifbp->IFB_FWSup.id == COMP_ID_APF && !mmd_check_comp( (void*)&cfg_drv_act_ranges_apf, &ifbp->IFB_FWSup) )
			) {                                                                                  /* 24 */
			ifbp->IFB_CardStat |= CARD_STAT_INCOMP_FW;
			rc = HCF_ERR_INCOMP_FW;
		}
	}
#endif // MSF_COMPONENT_ID

	if ( rc == HCF_SUCCESS && ifbp->IFB_FWIdentity.comp_id >= COMP_ID_FW_STA ) {
		PROT_CNT_INI;
		/**************************************************************************************
		 * rlav: the DMA engine needs the host to cause a 'hanging alloc event' for it to consume.
		 * not sure if this is the right spot in the HCF, thinking about hcf_enable...
		 **************************************************************************************/
		rc = cmd_exe( ifbp, HCMD_ALLOC, 0 );
// 180 degree error in logic ;? #if ALLOC_15
//		ifbp->IFB_RscInd = 1;   //let's hope that by the time hcf_send_msg isa called, there will be a FID
//#else
		if ( rc == HCF_SUCCESS ) {
			HCF_WAIT_WHILE( (IPW( HREG_EV_STAT ) & HREG_EV_ALLOC) == 0 );
			IF_PROT_TIME( HCFASSERT(prot_cnt, IPW( HREG_EV_STAT )) );
#if HCF_DMA
			if ( ! ( ifbp->IFB_CntlOpt & USE_DMA ) )
#endif // HCF_DMA
			{
				ifbp->IFB_RscInd = get_fid( ifbp );
				HCFASSERT( ifbp->IFB_RscInd, 0 );
				cmd_exe( ifbp, HCMD_ALLOC, 0 );
				IF_PROT_TIME( if ( prot_cnt == 0 ) rc = HCF_ERR_TIME_OUT );
			}
		}
//#endif // ALLOC_15
	}

	HCFASSERT( rc == HCF_SUCCESS, rc );
	HCFLOGEXIT( HCF_TRACE_INIT );
	return rc;
} // init

/************************************************************************************************************
 *
 *.SUBMODULE     void isr_info( IFBP ifbp )
 *.PURPOSE       handles link events.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 *
 *
 *.DIAGRAM
 *1: First the FID number corresponding with the InfoEvent is determined.
 *   Note the complication of the zero-FID protection sub-scheme in DAWA.
 *   Next the L-field and the T-field are fetched into scratch buffer info.
 *2: In case of tallies, the 16 bits Hermes values are accumulated in the IFB into 32 bits values. Info[0]
 *   is (expected to be) HCF_NIC_TAL_CNT + 1. The contraption "while ( info[0]-- >1 )" rather than
 *   "while ( --info[0] )" is used because it is dangerous to determine the length of the Value field by
 *   decrementing info[0]. As a result of a bug in some version of the F/W, info[0] may be 0, resulting
 *   in a very long loop in the pre-decrement logic.
 *4: In case of a link status frame, the information is copied to the IFB field IFB_linkStat
 *6: All other than Tallies (including "unknown" ones) are checked against the selection set by the MSF
 *   via CFG_RID_LOG. If a match is found or the selection set has the wild-card type (i.e non-NULL buffer
 *   pointer at the terminating zero-type), the frame is copied to the (type-specific) log buffer.
 *   Note that to accumulate tallies into IFB AND to log them or to log a frame when a specific match occures
 *   AND based on the wild-card selection, you have to call setup_bap again after the 1st copy.
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC void
isr_info( IFBP ifbp )
{
	hcf_16  info[2], fid;
#if (HCF_EXT) & HCF_EXT_INFO_LOG
	RID_LOGP    ridp = ifbp->IFB_RIDLogp;   //NULL or pointer to array of RID_LOG structures (terminated by zero typ)
#endif // HCF_EXT_INFO_LOG

	HCFTRACE( ifbp, HCF_TRACE_ISR_INFO );                                                               /* 1 */
	fid = IPW( HREG_INFO_FID );
	DAWA_ZERO_FID( HREG_INFO_FID );
	if ( fid ) {
		(void)setup_bap( ifbp, fid, 0, IO_IN );
		get_frag( ifbp, (wci_bufp)info, 4 BE_PAR(2) );
		HCFASSERT( info[0] <= HCF_MAX_LTV + 1, MERGE_2( info[1], info[0] ) );  //;? a smaller value makes more sense
#if (HCF_TALLIES) & HCF_TALLIES_NIC     //Hermes tally support
		if ( info[1] == CFG_TALLIES ) {
			hcf_32  *p;
		/*2*/   if ( info[0] > HCF_NIC_TAL_CNT ) {
				info[0] = HCF_NIC_TAL_CNT + 1;
			}
			p = (hcf_32*)&ifbp->IFB_NIC_Tallies;
			while ( info[0]-- >1 ) *p++ += IPW( HREG_DATA_1 );  //request may return zero length
		}
		else
#endif // HCF_TALLIES_NIC
		{
		/*4*/   if ( info[1] == CFG_LINK_STAT ) {
				ifbp->IFB_LinkStat = IPW( HREG_DATA_1 );
			}
#if (HCF_EXT) & HCF_EXT_INFO_LOG
		/*6*/   while ( 1 ) {
				if ( ridp->typ == 0 || ridp->typ == info[1] ) {
					if ( ridp->bufp ) {
						HCFASSERT( ridp->len >= 2, ridp->typ );
						ridp->bufp[0] = min((hcf_16)(ridp->len - 1), info[0] );     //save L
						ridp->bufp[1] = info[1];                        //save T
						get_frag( ifbp, (wci_bufp)&ridp->bufp[2], (ridp->bufp[0] - 1)*2 BE_PAR(0) );
					}
					break;
				}
				ridp++;
			}
#endif // HCF_EXT_INFO_LOG
		}
		HCFTRACE( ifbp, HCF_TRACE_ISR_INFO | HCF_TRACE_EXIT );
	}
	return;
} // isr_info

//
//
// #endif // HCF_TALLIES_NIC
// /*4*/    if ( info[1] == CFG_LINK_STAT ) {
//          ifbp->IFB_DSLinkStat = IPW( HREG_DATA_1 ) | CFG_LINK_STAT_CHANGE;   //corrupts BAP !! ;?
//          ifbp->IFB_LinkStat = ifbp->IFB_DSLinkStat & CFG_LINK_STAT_FW; //;? to be obsoleted
//          printk(KERN_ERR "linkstatus: %04x\n", ifbp->IFB_DSLinkStat );        //;?remove me 1 day
// #if (HCF_SLEEP) & HCF_DDS
//          if ( ( ifbp->IFB_DSLinkStat & CFG_LINK_STAT_CONNECTED ) == 0 ) {    //even values are disconnected etc.
//              ifbp->IFB_TickCnt = 0;              //start 2 second period (with 1 tick uncertanty)
//              printk(KERN_NOTICE "isr_info: AwaitConnection phase started, IFB_TickCnt = 0\n" );      //;?remove me 1 day
//          }
// #endif // HCF_DDS
//      }
// #if (HCF_EXT) & HCF_EXT_INFO_LOG
// /*6*/    while ( 1 ) {
//          if ( ridp->typ == 0 || ridp->typ == info[1] ) {
//              if ( ridp->bufp ) {
//                  HCFASSERT( ridp->len >= 2, ridp->typ );
//                  (void)setup_bap( ifbp, fid, 2, IO_IN );         //restore BAP for tallies, linkstat and specific type followed by wild card
//                  ridp->bufp[0] = min( ridp->len - 1, info[0] );  //save L
//                  get_frag( ifbp, (wci_bufp)&ridp->bufp[1], ridp->bufp[0]*2 BE_PAR(0) );
//              }
//              break; //;?this break is no longer needed due to setup_bap but lets concentrate on DDS first
//          }
//          ridp++;
//      }
// #endif // HCF_EXT_INFO_LOG
//  }
//  HCFTRACE( ifbp, HCF_TRACE_ISR_INFO | HCF_TRACE_EXIT );
//
//
//
//
//  return;
//} // isr_info


/************************************************************************************************************
 *
 *.SUBMODULE     void mdd_assert( IFBP ifbp, unsigned int line_number, hcf_32 q )
 *.PURPOSE       filters assert on level and interfaces to the MSF supplied msf_assert routine.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   line_number line number of the line which caused the assert
 *   q           qualifier, additional information which may give a clue about the problem
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 *
 *
 *.DIAGRAM
 *
 *.NOTICE
 * mdd_assert has been through a turmoil, renaming hcf_assert to assert and hcf_assert again and supporting off
 * and on being called from the MSF level and other ( immature ) ModularDriverDevelopment modules like DHF and
 * MMD.
 * !!!! The assert routine is not an hcf_..... routine in the sense that it may be called by the MSF,
 *      however it is called from mmd.c and dhf.c, so it must be external.
 *      To prevent namespace pollution it needs a prefix, to prevent that MSF programmers think that
 *      they are allowed to call the assert logic, the prefix HCF can't be used, so MDD is selected!!!!
 *
 * When called from the DHF module the line number is incremented by DHF_FILE_NAME_OFFSET and when called from
 * the MMD module by MMD_FILE_NAME_OFFSET.
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
#if HCF_ASSERT
void
mdd_assert( IFBP ifbp, unsigned int line_number, hcf_32 q )
{
	hcf_16  run_time_flag = ifbp->IFB_AssertLvl;

	if ( run_time_flag /* > ;?????? */ ) { //prevent recursive behavior, later to be extended to level filtering
		ifbp->IFB_AssertQualifier = q;
		ifbp->IFB_AssertLine = (hcf_16)line_number;
#if (HCF_ASSERT) & ( HCF_ASSERT_LNK_MSF_RTN | HCF_ASSERT_RT_MSF_RTN )
		if ( ifbp->IFB_AssertRtn ) {
			ifbp->IFB_AssertRtn( line_number, ifbp->IFB_AssertTrace, q );
		}
#endif // HCF_ASSERT_LNK_MSF_RTN / HCF_ASSERT_RT_MSF_RTN
#if (HCF_ASSERT) & HCF_ASSERT_SW_SUP
		OPW( HREG_SW_2, line_number );
		OPW( HREG_SW_2, ifbp->IFB_AssertTrace );
		OPW( HREG_SW_2, (hcf_16)q );
		OPW( HREG_SW_2, (hcf_16)(q >> 16 ) );
#endif // HCF_ASSERT_SW_SUP

#if (HCF_ASSERT) & HCF_ASSERT_MB
		ifbp->IFB_AssertLvl = 0;                                    // prevent recursive behavior
		hcf_put_info( ifbp, (LTVP)&ifbp->IFB_AssertStrct );
		ifbp->IFB_AssertLvl = run_time_flag;                        // restore appropriate filter level
#endif // HCF_ASSERT_MB
	}
} // mdd_assert
#endif // HCF_ASSERT


/************************************************************************************************************
 *
 *.SUBMODULE     void put_frag( IFBP ifbp, wci_bufp bufp, int len BE_PAR( int word_len ) )
 *.PURPOSE       writes with 16/32 bit I/O via BAP1 port from Host memory to NIC RAM.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   bufp        (byte) address of buffer
 *   len         length in bytes of buffer specified by bufp
 *   word_len    Big Endian only: number of leading bytes to swap in pairs
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * process the single byte (if applicable) not yet written by the previous put_frag and copy len
 * (or len-1) bytes from bufp to NIC.
 *
 *
 *.DIAGRAM
 *
 *.NOTICE
 *   It turns out DOS ODI uses zero length fragments. The HCF code can cope with it, but as a consequence, no
 *   Assert on len is possible
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC void
put_frag( IFBP ifbp, wci_bufp bufp, int len BE_PAR( int word_len ) )
{
	hcf_io      io_port = ifbp->IFB_IOBase + HREG_DATA_1;   //BAP data register
	int         i;                                          //prevent side effects from macro
	hcf_16      j;
	HCFASSERT( ((hcf_32)bufp & (HCF_ALIGN-1) ) == 0, (hcf_32)bufp );
#if HCF_BIG_ENDIAN
	HCFASSERT( word_len == 0 || word_len == 2 || word_len == 4, word_len );
	HCFASSERT( word_len == 0 || ((hcf_32)bufp & 1 ) == 0, (hcf_32)bufp );
	HCFASSERT( word_len <= len, MERGE_2( word_len, len ) );

	if ( word_len ) {                                   //if there is anything to convert
		                                //.  convert and write the 1st hcf_16
		j = bufp[1] | bufp[0]<<8;
		OUT_PORT_WORD( io_port, j );
		                                //.  update pointer and counter accordingly
		len -= 2;
		bufp += 2;
		if ( word_len > 1 ) {           //.  if there is to convert more than 1 word ( i.e 2 )
			                        //.  .  convert and write the 2nd hcf_16
			j = bufp[1] | bufp[0]<<8;   /*bufp is already incremented by 2*/
			OUT_PORT_WORD( io_port, j );
			                        //.  .  update pointer and counter accordingly
			len -= 2;
			bufp += 2;
		}
	}
#endif // HCF_BIG_ENDIAN
	i = len;
	if ( i && ifbp->IFB_CarryOut ) {                    //skip zero-length
		j = ((*bufp)<<8) + ( ifbp->IFB_CarryOut & 0xFF );
		OUT_PORT_WORD( io_port, j );
		bufp++; i--;
		ifbp->IFB_CarryOut = 0;
	}
#if (HCF_IO) & HCF_IO_32BITS
	//skip zero-length I/O, single byte I/O and I/O not worthwhile (i.e. less than 6 bytes)for DW logic
	                                                        //if buffer length >= 6 and 32 bits I/O support
	if ( !(ifbp->IFB_CntlOpt & USE_16BIT) && i >= 6 ) {
		hcf_32 FAR  *p4; //prevent side effects from macro
		if ( ( (hcf_32)bufp & 0x1 ) == 0 ) {            //.  if buffer at least word aligned
			if ( (hcf_32)bufp & 0x2 ) {             //.  .  if buffer not double word aligned
								//.  .  .  write a single word to get double word aligned
				j = *(wci_recordp)bufp;     //just to help ease writing macros with embedded assembly
				OUT_PORT_WORD( io_port, j );
				                                //.  .  .  adjust buffer length and pointer accordingly
				bufp += 2; i -= 2;
			}
			                                        //.  .  write as many double word as possible
			p4 = (hcf_32 FAR *)bufp;
			j = (hcf_16)i/4;
			OUT_PORT_STRING_32( io_port, p4, j );
			                                        //.  .  adjust buffer length and pointer accordingly
			bufp += i & ~0x0003;
			i &= 0x0003;
		}
	}
#endif // HCF_IO_32BITS
	                                        //if no 32-bit support OR byte aligned OR 1 word left
	if ( i ) {
		                                //.  if odd number of bytes left
		if ( i & 0x0001 ) {
			                        //.  .  save left over byte (before bufp is corrupted) in carry, set carry flag
			ifbp->IFB_CarryOut = (hcf_16)bufp[i-1] | 0x0100;    //note that i and bufp are always simultaneously modified, &bufp[i-1] is invariant
		}
		                                //.  write as many word as possible in "alignment safe" way
		j = (hcf_16)i/2;
		OUT_PORT_STRING_8_16( io_port, bufp, j );
	}
} // put_frag


/************************************************************************************************************
 *
 *.SUBMODULE     void put_frag_finalize( IFBP ifbp )
 *.PURPOSE       cleanup after put_frag for trailing odd byte and MIC transfer to NIC.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *
 *.RETURNS       N.A.
 *
 *.DESCRIPTION
 * finalize the MIC calculation with the padding pattern, output the last byte (if applicable)
 * of the message and the MIC to the TxFS
 *
 *
 *.DIAGRAM
 *2: 1 byte of the last put_frag may be still in IFB_CarryOut ( the put_frag carry holder ), so ........
 *   1 - 3 bytes of the last put_frag may be still in IFB_tx_32 ( the MIC engine carry holder ), so ........
 *   The call to the MIC calculation routine feeds these remaining bytes (if any) of put_frag and the
 *   just as many bytes of the padding as needed to the MIC calculation engine. Note that the "unneeded" pad
 *   bytes simply end up in the MIC engine carry holder and are never used.
 *8: write the remainder of the MIC and possible some garbage to NIC RAM
 *   Note: i is always 4 (a loop-invariant of the while in point 2)
 *
 *.NOTICE
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC void
put_frag_finalize( IFBP ifbp )
{
#if (HCF_TYPE) & HCF_TYPE_WPA
	if ( ifbp->IFB_MICTxCarry != 0xFFFF) {      //if MIC calculation active
		CALC_TX_MIC( mic_pad, 8);               //.  feed (up to 8 bytes of) virtual padding to MIC engine
		                                        //.  write (possibly) trailing byte + (most of) MIC
		put_frag( ifbp, (wci_bufp)ifbp->IFB_MICTx, 8 BE_PAR(0) );
	}
#endif // HCF_TYPE_WPA
	put_frag( ifbp, null_addr, 1 BE_PAR(0) );   //write (possibly) trailing data or MIC byte
} // put_frag_finalize


/************************************************************************************************************
 *
 *.SUBMODULE     int put_info( IFBP ifbp, LTVP ltvp )
 *.PURPOSE       support routine to handle the "basic" task of hcf_put_info to pass RIDs to the NIC.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   ltvp        address in NIC RAM where LVT-records are located
 *
 *.RETURNS
 *   HCF_SUCCESS
 *   >>put_frag
 *   >>cmd_wait
 *
 *.DESCRIPTION
 *
 *
 *.DIAGRAM
 *20: do not write RIDs to NICs which have incompatible Firmware
 *24: If the RID does not exist, the L-field is set to zero.
 *   Note that some RIDs can not be read, e.g. the pseudo RIDs for direct Hermes commands and CFG_DEFAULT_KEYS
 *28: If the RID is written successful, pass it to the NIC by means of an Access Write command
 *
 *.NOTICE
 *   The mechanism to HCF_ASSERT on invalid typ-codes in the LTV record is based on the following strategy:
 *     - some codes (e.g. CFG_REG_MB) are explicitly handled by the HCF which implies that these codes
 *       are valid. These codes are already consumed by hcf_put_info.
 *     - all other codes are passed to the Hermes. Before the put action is executed, hcf_get_info is called
 *       with an LTV record with a value of 1 in the L-field and the intended put action type in the Typ-code
 *       field. If the put action type is valid, it is also valid as a get action type code - except
 *       for CFG_DEFAULT_KEYS and CFG_ADD_TKIP_DEFAULT_KEY - so the HCF_ASSERT logic of hcf_get_info should
 *       not catch.
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC int
put_info( IFBP ifbp, LTVP ltvp  )
{

	int rc = HCF_SUCCESS;

	HCFASSERT( ifbp->IFB_CardStat == 0, MERGE_2( ltvp->typ, ifbp->IFB_CardStat ) );
	HCFASSERT( CFG_RID_CFG_MIN <= ltvp->typ && ltvp->typ <= CFG_RID_CFG_MAX, ltvp->typ );

	if ( ifbp->IFB_CardStat == 0 &&                                                             /* 20*/
	     ( ( CFG_RID_CFG_MIN <= ltvp->typ    && ltvp->typ <= CFG_RID_CFG_MAX ) ||
	       ( CFG_RID_ENG_MIN <= ltvp->typ /* && ltvp->typ <= 0xFFFF */       )     ) ) {
#if HCF_ASSERT //FCC8, FCB0, FCB4, FCB6, FCB7, FCB8, FCC0, FCC4, FCBC, FCBD, FCBE, FCBF
		{
			hcf_16     t = ltvp->typ;
			LTV_STRCT  x = { 2, t, {0} };                                                          /*24*/
			hcf_get_info( ifbp, (LTVP)&x );
			if ( x.len == 0 &&
			     ( t != CFG_DEFAULT_KEYS && t != CFG_ADD_TKIP_DEFAULT_KEY && t != CFG_REMOVE_TKIP_DEFAULT_KEY &&
			       t != CFG_ADD_TKIP_MAPPED_KEY && t != CFG_REMOVE_TKIP_MAPPED_KEY &&
			       t != CFG_HANDOVER_ADDR && t != CFG_DISASSOCIATE_ADDR &&
			       t != CFG_FCBC && t != CFG_FCBD && t != CFG_FCBE && t != CFG_FCBF &&
			       t != CFG_DEAUTHENTICATE_ADDR
				     )
				) {
				HCFASSERT( DO_ASSERT, ltvp->typ );
			}
		}
#endif // HCF_ASSERT

		rc = setup_bap( ifbp, ltvp->typ, 0, IO_OUT );
		put_frag( ifbp, (wci_bufp)ltvp, 2*ltvp->len + 2 BE_PAR(2) );
	/*28*/  if ( rc == HCF_SUCCESS ) {
			rc = cmd_exe( ifbp, HCMD_ACCESS + HCMD_ACCESS_WRITE, ltvp->typ );
		}
	}
	return rc;
} // put_info


/************************************************************************************************************
 *
 *.SUBMODULE     int put_info_mb( IFBP ifbp, CFG_MB_INFO_STRCT FAR * ltvp )
 *.PURPOSE       accumulates a ( series of) buffers into a single Info block into the MailBox.
 *
 *.ARGUMENTS
 *   ifbp        address of the Interface Block
 *   ltvp        address of structure specifying the "type" and the fragments of the information to be synthesized
 *               as an LTV into the MailBox
 *
 *.RETURNS
 *
 *.DESCRIPTION
 * If the data does not fit (including no MailBox is available), the IFB_MBTally is incremented and an
 * error status is returned.
 * HCF_ASSERT does not catch.
 * Calling put_info_mb when their is no MailBox available, is considered a design error in the MSF.
 *
 * Note that there is always at least 1 word of unused space in the mail box.
 * As a consequence:
 * - no problem in pointer arithmetic (MB_RP == MB_WP means unambiguously mail box is completely empty
 * - There is always free space to write an L field with a value of zero after each MB_Info block.  This
 *   allows for an easy scan mechanism in the "get MB_Info block" logic.
 *
 *
 *.DIAGRAM
 *1: Calculate L field of the MBIB, i.e. 1 for the T-field + the cumulative length of the fragments.
 *2: The free space in the MailBox is calculated (2a: free part from Write Ptr to Read Ptr, 2b: free part
 *   turns out to wrap around) . If this space suffices to store the number of words reflected by len (T-field
 *   + Value-field) plus the additional MailBox Info L-field + a trailing 0 to act as the L-field of a trailing
 *   dummy or empty LTV record, then a MailBox Info block is build in the MailBox consisting of
 *     - the value len in the first word
 *     - type in the second word
 *     - a copy of the contents of the fragments in the second and higher word
 *
 *4: Since put_info_mb() can more or less directly be called from the MSF level, the I/F must be robust
 *   against out-of-range variables. As failsafe coding, the MB update is skipped by changing tlen to 0 if
 *   len == 0; This will indirectly cause an assert as result of the violation of the next if clause.
 *6: Check whether the free space in MailBox suffices (this covers the complete absence of the MailBox).
 *   Note that len is unsigned, so even MSF I/F violation works out O.K.
 *   The '2' in the expression "len+2" is used because 1 word is needed for L itself and 1 word is needed
 *   for the zero-sentinel
 *8: update MailBox Info length report to MSF with "oldest" MB Info Block size. Be careful here, if you get
 *   here before the MailBox is registered, you can't read from the buffer addressed by IFB_MBp (it is the
 *   Null buffer) so don't move this code till the end of this routine but keep it where there is garuanteed
 *   a buffer.
 *
 *.NOTICE
 *   boundary testing depends on the fact that IFB_MBSize is guaranteed to be zero if no MailBox is present,
 *   and to a lesser degree, that IFB_MBWp = IFB_MBRp = 0
 *
 *.ENDDOC                END DOCUMENTATION
 *
 ************************************************************************************************************/

HCF_STATIC int
put_info_mb( IFBP ifbp, CFG_MB_INFO_STRCT FAR * ltvp )
{

	int         rc = HCF_SUCCESS;
	hcf_16      i;                      //work counter
	hcf_16      *dp;                    //destination pointer (in MailBox)
	wci_recordp sp;                     //source pointer
	hcf_16      len;                    //total length to copy to MailBox
	hcf_16      tlen;                   //free length/working length/offset in WMP frame

	if ( ifbp->IFB_MBp == NULL ) return rc;  //;?not sufficient
	HCFASSERT( ifbp->IFB_MBp != NULL, 0 );                   //!!!be careful, don't get into an endless recursion
	HCFASSERT( ifbp->IFB_MBSize, 0 );

	len = 1;                                                                                            /* 1 */
	for ( i = 0; i < ltvp->frag_cnt; i++ ) {
		len += ltvp->frag_buf[i].frag_len;
	}
	if ( ifbp->IFB_MBRp > ifbp->IFB_MBWp ) {
		tlen = ifbp->IFB_MBRp - ifbp->IFB_MBWp;                                                         /* 2a*/
	} else {
		if ( ifbp->IFB_MBRp == ifbp->IFB_MBWp ) {
			ifbp->IFB_MBRp = ifbp->IFB_MBWp = 0;    // optimize Wrapping
		}
		tlen = ifbp->IFB_MBSize - ifbp->IFB_MBWp;                                                       /* 2b*/
		if ( ( tlen <= len + 2 ) && ( len + 2 < ifbp->IFB_MBRp ) ) {    //if trailing space is too small but
			                                                        //   leading space is sufficiently large
			ifbp->IFB_MBp[ifbp->IFB_MBWp] = 0xFFFF;                 //flag dummy LTV to fill the trailing space
			ifbp->IFB_MBWp = 0;                                     //reset WritePointer to begin of MailBox
			tlen = ifbp->IFB_MBRp;                                  //get new available space size
		}
	}
	dp = &ifbp->IFB_MBp[ifbp->IFB_MBWp];
	if ( len == 0 ) {
		tlen = 0; //;? what is this good for
	}
	if ( len + 2 >= tlen ){                                                                             /* 6 */
		//Do Not ASSERT, this is a normal condition
		IF_TALLY( ifbp->IFB_HCF_Tallies.NoBufMB++ );
		rc = HCF_ERR_LEN;
	} else {
		*dp++ = len;                                    //write Len (= size of T+V in words to MB_Info block
		*dp++ = ltvp->base_typ;                         //write Type to MB_Info block
		ifbp->IFB_MBWp += len + 1;                      //update WritePointer of MailBox
		for ( i = 0; i < ltvp->frag_cnt; i++ ) {                // process each of the fragments
			sp = ltvp->frag_buf[i].frag_addr;
			len = ltvp->frag_buf[i].frag_len;
			while ( len-- ) *dp++ = *sp++;
		}
		ifbp->IFB_MBp[ifbp->IFB_MBWp] = 0;              //to assure get_info for CFG_MB_INFO stops
		ifbp->IFB_MBInfoLen = ifbp->IFB_MBp[ifbp->IFB_MBRp];                                            /* 8 */
	}
	return rc;
} // put_info_mb


/************************************************************************************************************
 *
 *.SUBMODULE     int setup_bap( IFBP ifbp, hcf_16 fid, int offset, int type )
 *.PURPOSE       set up data access to NIC RAM via BAP_1.
 *
 *.ARGUMENTS
 *   ifbp            address of I/F Block
 *   fid             FID/RID
 *   offset          !!even!! offset in FID/RID
 *   type            IO_IN, IO_OUT
 *
 *.RETURNS
 *   HCF_SUCCESS                 O.K
 *   HCF_ERR_NO_NIC              card is removed
 *   HCF_ERR_DEFUNCT_TIME_OUT    Fatal malfunction detected
 *   HCF_ERR_DEFUNCT_.....       if and only if IFB_DefunctStat <> 0
 *
 *.DESCRIPTION
 *
 * A non-zero return status indicates:
 * - the NIC is considered nonoperational, e.g. due to a time-out of some Hermes activity in the past
 * - BAP_1 could not properly be initialized
 * - the card is removed before completion of the data transfer
 * In all other cases, a zero is returned.
 * BAP Initialization failure indicates an H/W error which is very likely to signal complete H/W failure.
 * Once a BAP Initialization failure has occurred all subsequent interactions with the Hermes will return a
 * "defunct" status till the Hermes is re-initialized by means of an hcf_connect.
 *
 * A BAP is a set of registers (Offset, Select and Data) offering read/write access to a particular FID or
 * RID. This access is based on a auto-increment feature.
 * There are two BAPs but these days the HCF uses only BAP_1 and leaves BAP_0 to the PCI Busmastering H/W.
 *
 * The BAP-mechanism is based on the Busy bit in the Offset register (see the Hermes definition). The waiting
 * for Busy must occur between writing the Offset register and accessing the Data register. The
 * implementation to wait for the Busy bit drop after each write to the Offset register, implies that the
 * requirement that the Busy bit is low  before the Select register is written, is automatically met.
 * BAP-setup may be time consuming (e.g. 380 usec for large offsets occurs frequently). The wait for Busy bit
 * drop is protected by a loop counter, which is initialized with IFB_TickIni, which is calibrated in init.
 *
 * The NIC I/F is optimized for word transfer and can only handle word transfer at a word boundary in NIC
 * RAM. The intended solution for transfer of a single byte has multiple H/W flaws. There have been different
 * S/W Workaround strategies. RID access is hcf_16 based by "nature", so no byte access problems.  For Tx/Rx
 * FID access,  the byte logic became obsolete by absorbing it in the double word oriented nature of the MIC
 * feature.
 *
 *
 *.DIAGRAM
 *
 *2: the test on rc checks whether the HCF went into "defunct" mode ( e.g. BAP initialization or a call to
 *   cmd_wait did ever fail).
 *4: the select register and offset register are set
 *   the offset register is monitored till a successful condition (no busy bit) is detected or till the
 *   (calibrated) protection counter expires
 *   If the counter expires, this is reflected in IFB_DefunctStat, so all subsequent calls to setup_bap fail
 *   immediately ( see 2)
 *6: initialization of the carry as used by pet/get_frag
 *8: HREG_OFFSET_ERR is ignored as error because:
 *    a: the Hermes is robust against it
 *    b: it is not known what causes it (probably a bug), hence no strategy can be specified which level is
 *       to handle this error in which way. In the past, it could be induced by the MSF level, e.g. by calling
 *       hcf_rcv_msg while there was no Rx-FID available. Since this is an MSF-error which is caught by ASSERT,
 *       there is no run-time action required by the HCF.
 *   Lumping the Offset error in with the Busy bit error, as has been done in the past turns out to be a
 *   disaster or a life saver, just depending on what the cause of the error is. Since no prediction can be
 *   done about the future, it is "felt" to be the best strategy to ignore this error. One day the code was
 *   accompanied by the following comment:
 *   //  ignore HREG_OFFSET_ERR, someone, supposedly the MSF programmer ;) made a bug. Since we don't know
 *   //  what is going on, we might as well go on - under management pressure - by ignoring it
 *
 *.ENDDOC                          END DOCUMENTATION
 *
 ************************************************************************************************************/
HCF_STATIC int
setup_bap( IFBP ifbp, hcf_16 fid, int offset, int type )
{
	PROT_CNT_INI;
	int rc;

	HCFTRACE( ifbp, HCF_TRACE_STRIO );
	rc = ifbp->IFB_DefunctStat;
	if (rc == HCF_SUCCESS) {                                        /*2*/
		OPW( HREG_SELECT_1, fid );                                                              /*4*/
		OPW( HREG_OFFSET_1, offset );
		if ( type == IO_IN ) {
			ifbp->IFB_CarryIn = 0;
		}
		else ifbp->IFB_CarryOut = 0;
		HCF_WAIT_WHILE( IPW( HREG_OFFSET_1) & HCMD_BUSY );
		HCFASSERT( !( IPW( HREG_OFFSET_1) & HREG_OFFSET_ERR ), MERGE_2( fid, offset ) );         /*8*/
		if ( prot_cnt == 0 ) {
			HCFASSERT( DO_ASSERT, MERGE_2( fid, offset ) );
			rc = ifbp->IFB_DefunctStat = HCF_ERR_DEFUNCT_TIME_OUT;
			ifbp->IFB_CardStat |= CARD_STAT_DEFUNCT;
		}
	}
	HCFTRACE( ifbp, HCF_TRACE_STRIO | HCF_TRACE_EXIT );
	return rc;
} // setup_bap

