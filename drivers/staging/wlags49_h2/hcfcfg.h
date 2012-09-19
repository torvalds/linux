
#ifndef HCFCFG_H
#define HCFCFG_H 1

/*************************************************************************************************************
*
* FILE	 : hcfcfg.tpl // hcfcfg.h
*
* DATE   : $Date: 2004/08/05 11:47:10 $   $Revision: 1.6 $
* Original: 2004/04/08 15:18:16    Revision: 1.40      Tag: t20040408_01
* Original: 2004/04/01 15:32:55    Revision: 1.38      Tag: t7_20040401_01
* Original: 2004/03/10 15:39:28    Revision: 1.34      Tag: t20040310_01
* Original: 2004/03/03 14:10:12    Revision: 1.32      Tag: t20040304_01
* Original: 2004/03/02 09:27:12    Revision: 1.30      Tag: t20040302_03
* Original: 2004/02/24 13:00:28    Revision: 1.25      Tag: t20040224_01
* Original: 2004/02/18 17:13:57    Revision: 1.23      Tag: t20040219_01
*
* AUTHOR : Nico Valster
*
* DESC   : HCF Customization Macros
* hcfcfg.tpl list all #defines which must be specified to:
*   adjust the HCF functions defined in HCF.C to the characteristics of a specific environment
*		o maximum sizes for messages
*		o Endianess
*	Compiler specific macros
*		o port I/O macros
*		o type definitions
*
* By copying HCFCFG.TPL to HCFCFG.H and -if needed- modifying the #defines the WCI functionality can be
* tailored
*
* Supported environments:
* WVLAN_41	Miniport								NDIS 3.1
* WVLAN_42	Packet									Microsoft Visual C 1.5
* WVLAN_43	16 bits DOS ODI							Microsoft Visual C 1.5
* WVLAN_44	32 bits ODI (__NETWARE_386__)			WATCOM
* WVLAN_45	MAC_OS									MPW?, Symantec?
* WVLAN_46	Windows CE (_WIN32_WCE)					Microsoft ?
* WVLAN_47	LINUX  (__LINUX__)						GCC, discarded, based on GPL'ed HCF-light
* WVLAN_48	Miniport								NDIS 5
* WVLAN_49	LINUX  (__LINUX__)						GCC, originally based on pre-compiled HCF_library
* 													migrated to use the HCF sources when Lucent Technologies
* 													brought the HCF module under GPL
* WVLAN_51	Miniport USB							NDIS 5
* WVLAN_52	Miniport								NDIS 4
* WVLAN_53	VxWorks END Station driver
* WVLAN_54	VxWorks END Access Point driver
* WVLAN_81	WavePoint								BORLAND C
* WCITST	Inhouse test tool						Microsoft Visual C 1.5
* WSU		WaveLAN Station Update					Microsoft Visual C ??
* SCO UNIX	not yet actually used ?					?
* __ppc		OEM supplied							?
* _AM29K	OEM supplied							?
* ?			OEM supplied							Microtec Research 80X86 Compiler
*
**************************************************************************************************************
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
*************************************************************************************************************/

/*	Alignment
*	Some platforms can access words on odd boundaries (with possibly an performance impact), at other
*	platforms such an access may result in a memory access violation.
*	It is assumed that everywhere where the HCF casts a char pointer into a word pointer, the alignment
*	criteria are met. This put some restrictions on the MSF, which are assumed to be "automatically" fulfilled
*	at the applicable platforms
*	To assert this assumption, the macro HCF_ALIGN can be defined. The default value is 1, meaning byte
*	alignment (or no alignment), a value of 2 means word alignment, a value of 4 means double word alignment
*/

/*****************************    IN_PORT_STRING_8_16  S a m p l e s *****************************************

		// C implementation which let the processor handle the word-at-byte-boundary problem
#define IN_PORT_STRING_8_16( port, addr, n) while ( n-- ) 	\
	{ *(hcf_16 FAR*)addr = IN_PORT_WORD( port ); ((hcf_8 FAR*)addr)+=2; }

		// C implementation which handles the word-at-byte-boundary problem
#define IN_PORT_STRING_8_16( port, addr, n)	while ( n-- )	 \
	{ hcf_16 i = IN_PORT_WORD(port); *((hcf_8 FAR*)addr)++ = (hcf_8)i; *((hcf_8 FAR*)addr)++ = (hcf_8)(i>>8);}

		// Assembler implementation
#define IN_PORT_STRING_8_16( port, addr, len) __asm		\
{														\
	__asm push di										\
	__asm push es										\
	__asm mov cx,len									\
	__asm les di,addr									\
	__asm mov dx,port									\
	__asm rep insw										\
	__asm pop es										\
	__asm pop di										\
}


*****************************   OUT_PORT_STRING_8_16  S a m p l e s ******************************************

		// C implementation which let the processor handle the word-at-byte-boundary problem
#define OUT_PORT_STRING_8_16( port, addr, n)	while ( n-- ) \
	{ OUT_PORT_WORD( port, *(hcf_16 FAR*)addr ) ; ((hcf_8 FAR*)addr)+=2; }

		// C implementation which handles the word-at-byte-boundary problem
#define OUT_PORT_STRING_8_16( port, addr, n)	while ( n-- ) \
	{ OUT_PORT_WORD( port, *((hcf_8 FAR*)addr) | *(((hcf_8 FAR*)addr)+1)<<8  ); (hcf_8 FAR*)addr+=2; }

		// Assembler implementation
#define OUT_PORT_STRING_8_16( port, addr, len) __asm	\
{														\
	__asm push si										\
	__asm push ds										\
	__asm mov cx,len									\
	__asm lds si,addr									\
	__asm mov dx,port									\
	__asm rep outsw										\
	__asm pop ds										\
	__asm pop si										\
}

*************************************************************************************************************/


/************************************************************************************************/
/******************  C O M P I L E R   S P E C I F I C   M A C R O S  ***************************/
/************************************************************************************************/
/*************************************************************************************************
*
*			!!!!!!!!!!!!!!!!!!!!!!!!! Note to the HCF-implementor !!!!!!!!!!!!!!!!!!!!!!!!!
*			!!!! Do not call these macros with parameters which introduce side effects !!!!
*			!!!!!!!!!!!!!!!!!!!!!!!!! Note to the HCF-implementor !!!!!!!!!!!!!!!!!!!!!!!!!
*
*
* By selecting the appropriate Macro definitions by means of modifying the "#ifdef 0/1" lines, the HCF can be
* adjusted for the I/O characteristics of a specific compiler
*
* If needed the macros can be modified or replaced with definitions appropriate for your personal platform.
* If you need to make such changes it is appreciated if you inform Agere Systems
* That way the changes can become part of the next release of the WCI
*
* For convenience of the MSF-programmer, all macros are allowed to modify their parameters (although some
* might argue that this would constitute bad coding practice). This has its implications on the HCF, e.g. as a
* consequence these macros should not be called with parameters which have side effects, e.g auto-increment.
*
* in the Microsoft implementation of inline assembly it is O.K. to corrupt all flags except the direction flag
* and to corrupt all registers except the segment registers and EDI, ESI, ESP and EBP (or their 16 bits
* equivalents).  Other environments may have other constraints
*
* in the Intel environment it is O.K to have a word (as a 16 bits quantity) at a byte boundary, hence
* IN_/OUT_PORT_STRING_8_16 can move words between PC-memory and NIC-memory with as only constraint that the
* words are on a word boundary in NIC-memory. This does not hold true for all conceivable environments, e.g.
* an Motorola 68xxx does not allow this. Probably/hopefully the boundary conditions imposed by these type of
* platforms prevent this case from materializing.  If this is not the case, OUT_PORT_STRING_8_16 must be coded
* by combining two Host memory hcf_8 values at a time to a single hcf_16 value to be passed to the NIC and
* IN_PORT_STRING_8_16 the single hcf_16 retrieved from the NIC must be split in two hcf_8 values to be stored
* in Host memory (see the sample code above)
*
*	The prototypes and functional description of the macros are:
*
*	hcf_16	IN_PORT_WORD( hcf_16 port )
*			Reads a word (16 bits) from the specified port
*
*	void	OUT_PORT_WORD( hcf_16 port, hcf_16 value)
*			Writes a word (16 bits) to the specified port
*
*	hcf_16	IN_PORT_DWORD( hcf_16 port )
*			Reads a dword (32 bits) from the specified port
*
*	void	OUT_PORT_DWORD( hcf_16 port, hcf_32 value)
*			Writes a dword (32 bits) to the specified port
*
*	void	IN_PORT_STRING_8_16( port, addr, len)
*			Reads len number of words (16 bits) from NIC memory via the specified port to the (FAR)
*			byte-pointer addr in PC-RAM
*			Note that len specifies the number of words, NOT the number of bytes
*			!!!NOTE, although len specifies the number of words, addr MUST be a char pointer NOTE!!!
*			See also the common notes for IN_PORT_STRING_8_16 and OUT_PORT_STRING_8_16
*
*	void	OUT_PORT_STRING_8_16( port, addr, len)
*			Writes len number of words (16 bits) from the (FAR) byte-pointer addr in PC-RAM via the specified
*			port to NIC memory
*			Note that len specifies the number of words, NOT the number of bytes.
*			!!!NOTE, although len specifies the number of words, addr MUST be a char pointer NOTE!!!
*
*			The peculiar combination of word-length and char pointers for IN_PORT_STRING_8_16 as well as
*			OUT_PORT_STRING_8_16 is justified by the assumption that it offers a more optimal algorithm
*
*	void	IN_PORT_STRING_32( port, addr, len)
*			Reads len number of double-words (32 bits) from NIC memory via the specified port to the (FAR)
*			double-word address addr in PC-RAM
*
*	void	OUT_PORT_STRING_32( port, addr, len)
*			Writes len number of double-words (32 bits) from the (FAR) double-word address addr in PC-RAM via
*			the specified port to NIC memory
*
*			!!!!!!!!!!!!!!!!!!!!!!!!! Note to the HCF-implementor !!!!!!!!!!!!!!!!!!!!!!!!!
*			!!!! Do not call these macros with parameters which introduce side effects !!!!
*			!!!!!!!!!!!!!!!!!!!!!!!!! Note to the HCF-implementor !!!!!!!!!!!!!!!!!!!!!!!!!
*
*************************************************************************************************/

/****************************  define INT Types  ******************************/
typedef unsigned char			hcf_8;
typedef unsigned short			hcf_16;
typedef unsigned long			hcf_32;

/****************************  define I/O Types  ******************************/
#define HCF_IO_MEM     			0x0001	// memory mapped I/O	( 0: Port I/O )
#define HCF_IO_32BITS   	  	0x0002 	// 32Bits support		( 0: only 16 Bits I/O)

/****************************** #define HCF_TYPE ********************************/
#define HCF_TYPE_NONE			0x0000	// No type
#define HCF_TYPE_WPA			0x0001	// WPA support
#define HCF_TYPE_USB			0x0002	// reserved (USB Dongle driver support)
//#define HCF_TYPE_HII			0x0004	// Hermes-II, to discriminate H-I and H-II CFG_HCF_OPT_STRCT
#define HCF_TYPE_WARP			0x0008	// WARP F/W
#define HCF_TYPE_PRELOADED		0x0040	// pre-loaded F/W
#define HCF_TYPE_HII5			0x0080	// Hermes-2.5 H/W
#define HCF_TYPE_CCX			0x0100	// CKIP
#define HCF_TYPE_BEAGLE_HII5	0x0200	// Beagle Hermes-2.5 H/W
#define HCF_TYPE_TX_DELAY		0x4000	// Delayed transmission ( non-DMA only)

/****************************** #define HCF_ASSERT ******************************/
#define HCF_ASSERT_NONE			0x0000	// No assert support
#define HCF_ASSERT_PRINTF		0x0001	// Hermes generated debug info
#define HCF_ASSERT_SW_SUP		0x0002	// logging via Hermes support register
#define HCF_ASSERT_MB			0x0004	// logging via Mailbox
#define HCF_ASSERT_RT_MSF_RTN	0x4000	// dynamically binding of msf_assert routine
#define HCF_ASSERT_LNK_MSF_RTN	0x8000	// statically binding of msf_assert routine

/****************************** #define HCF_ENCAP *******************************/
#define HCF_ENC_NONE			0x0000	// No encapsulation support
#define HCF_ENC					0x0001	// HCF handles En-/Decapsulation
#define HCF_ENC_SUP				0x0002	// HCF supports MSF to handle En-/Decapsulation

/****************************** #define HCF_EXT *********************************/
#define HCF_EXT_NONE			0x0000	// No expanded features
#define HCF_EXT_INFO_LOG		0x0001	// logging of Hermes Info frames
//#define HCF_EXT_INT_TX_OK		0x0002	// RESERVED!!! monitoring successful Tx message
#define HCF_EXT_INT_TX_EX		0x0004	// monitoring unsuccessful Tx message
//#define HCF_EXT_MON_MODE		0x0008	// LEGACY
#define HCF_EXT_TALLIES_FW		0x0010	// support for up to 32 Hermes Engineering tallies
#define HCF_EXT_TALLIES_HCF		0x0020	// support for up to 8 HCF Engineering tallies
#define HCF_EXT_NIC_ACCESS		0x0040	// direct access via Aux-ports and to Hermes registers and commands
#define HCF_EXT_MB				0x0080	// MailBox code expanded
#define HCF_EXT_IFB_STRCT 		0x0100	// MSF custom pointer in IFB
#define HCF_EXT_DESC_STRCT 		0x0200	// MSF custom pointer in Descriptor
#define HCF_EXT_TX_CONT			0x4000	// Continuous transmit test
#define HCF_EXT_INT_TICK		0x8000	// enables TimerTick interrupt generation

/****************************** #define HCF_SLEEP *******************************/
#define HCF_DDS					0x0001	// Disconnected Deep Sleep
#define HCF_CDS					0x0002	// Connected Deep Sleep

/****************************** #define HCF_TALLIES ******************************/
#define HCF_TALLIES_NONE		0x0000	// No tally support
#define HCF_TALLIES_NIC			0x0001	// Hermes Tallies accumulated in IFB
#define HCF_TALLIES_HCF			0x0002	// HCF Tallies accumulated in IFB
#define HCF_TALLIES_RESET		0x8000	// Tallies in IFB are reset when reported via hcf_get_info

/************************************************************************************************/
/******************************************  L I N U X  *****************************************/
/************************************************************************************************/

#ifdef WVLAN_49
#include <asm/io.h>
//#include <linux/module.h>
#include <wl_version.h>

/* The following macro ensures that no symbols are exported, minimizing the chance of a symbol
   collision in the kernel */
//EXPORT_NO_SYMBOLS;  //;?this place seems not appropriately to me

//#define HCF_SLEEP (HCF_CDS | HCF_DDS )
#define HCF_SLEEP (HCF_CDS)

/* Note: Non-WARP firmware all support WPA. However the original Agere
 * linux driver does not enable WPA. Enabling WPA here causes whatever
 * preliminary WPA logic to be included, some of which may be specific
 * to HERMESI.
 *
 * Various comment are clear that WARP and WPA are not compatible
 * (which may just mean WARP does WPA in a different fashion).
 */

/* #define HCF_TYPE    (HCF_TYPE_HII5|HCF_TYPE_STA|HCF_TYPE_AP) */
#ifdef HERMES25
#ifdef WARP
#define HCF_TYPE    ( HCF_TYPE_WARP | HCF_TYPE_HII5 )
#else
#define HCF_TYPE    (HCF_TYPE_HII5 | HCF_TYPE_WPA)
#endif /* WARP */
#else
#define HCF_TYPE    HCF_TYPE_WPA
#endif /* HERMES25 */

#ifdef ENABLE_DMA
#define HCF_DMA		1
#endif // ENABLE_DMA

/* We now need a switch to include support for the Mailbox and other necessary extensions */
#define HCF_EXT ( HCF_EXT_MB | HCF_EXT_INFO_LOG | HCF_EXT_INT_TICK )//get deepsleep exercise going

/* ;? The Linux MSF still uses these definitions; define it here until it's removed */
#ifndef HCF_TYPE_HII
#define HCF_TYPE_HII 0x0004
#endif

#ifndef HCF_TYPE_AP
#define HCF_TYPE_AP  0x0010
#endif

#ifndef HCF_TYPE_STA
#define HCF_TYPE_STA 0x0020
#endif  // HCF_TYPE_STA

/* Guarantees word alignment */
#define HCF_ALIGN		2

/* Endian macros CNV_INT_TO_LITTLE() and CNV_LITTLE_TO_INT() were renamed to
   CNV_SHORT_TO_LITTLE() and CNV_LITTLE_TO_SHORT() */
#ifndef CNV_INT_TO_LITTLE
#define CNV_INT_TO_LITTLE   CNV_SHORT_TO_LITTLE
#endif

#ifndef CNV_LITTLE_TO_INT
#define CNV_LITTLE_TO_INT   CNV_LITTLE_TO_SHORT
#endif

#define	HCF_ERR_BUSY			0x06

/* UIL defines were removed from the HCF */
#define UIL_SUCCESS					HCF_SUCCESS
#define UIL_ERR_TIME_OUT			HCF_ERR_TIME_OUT
#define UIL_ERR_NO_NIC				HCF_ERR_NO_NIC
#define UIL_ERR_LEN					HCF_ERR_LEN
#define UIL_ERR_MIN					HCF_ERR_MAX	/*end of HCF errors which are passed through to UIL
												  *** ** *** ****** ***** *** ****** ******* ** ***  */
#define UIL_ERR_IN_USE				0x44
#define UIL_ERR_WRONG_IFB			0x46
#define UIL_ERR_MAX					0x7F		/*upper boundary of UIL errors without HCF-pendant
                                                  ***** ******** ** *** ****** ******* *** *******  */
#define UIL_ERR_BUSY			    HCF_ERR_BUSY
#define UIL_ERR_DIAG_1			    HCF_ERR_DIAG_1
#define UIL_FAILURE					0xFF	/* 20010705 nv this relick should be eridicated */
#define UIL_ERR_PIF_CONFLICT		0x40	//obsolete
#define UIL_ERR_INCOMP_DRV			0x41	//obsolete
#define UIL_ERR_DOS_CALL			0x43	//obsolete
#define UIL_ERR_NO_DRV				0x42	//obsolete
#define UIL_ERR_NSTL				0x45	//obsolete



#if 0  //;? #ifdef get this going LATER HERMES25
#define HCF_IO              HCF_IO_32BITS
#define HCF_DMA             1
#define HCF_DESC_STRCT_EXT  4

/* Switch for BusMaster DMA support. Note that the above define includes the DMA-specific HCF
   code in the build. This define sets the MSF to use DMA; if ENABLE_DMA is not defined, then
   port I/O will be used in the build */
#ifndef BUS_PCMCIA
#define ENABLE_DMA
#endif  // USE_PCMCIA

#endif  // HERMES25


/* Overrule standard WaveLAN Packet Size when in DMA mode */
#ifdef ENABLE_DMA
#define HCF_MAX_PACKET_SIZE 2304
#else
#define HCF_MAX_PACKET_SIZE 1514
#endif  // ENABLE_DMA

/* The following sets the component ID, as well as the versioning. See also wl_version.h */
#define	MSF_COMPONENT_ID	COMP_ID_LINUX

#define	MSF_COMPONENT_VAR			DRV_VARIANT
#define MSF_COMPONENT_MAJOR_VER     DRV_MAJOR_VERSION
#define MSF_COMPONENT_MINOR_VER     DRV_MINOR_VERSION

/* Define the following to turn on assertions in the HCF */
//#define HCF_ASSERT  0x8000
#define HCF_ASSERT			HCF_ASSERT_LNK_MSF_RTN	// statically binding of msf_assert routine

#ifdef USE_BIG_ENDIAN
#define HCF_BIG_ENDIAN  1
#else
#define HCF_BIG_ENDIAN  0
#endif  /* USE_BIG_ENDIAN */

/* Define the following if your system uses memory-mapped IO */
//#define HCF_MEM_IO

/* The following defines the standard macros required by the HCF to move data to/from the card */
#define IN_PORT_BYTE(port)			((hcf_8)inb( (hcf_io)(port) ))
#define IN_PORT_WORD(port)			((hcf_16)inw( (hcf_io)(port) ))
#define OUT_PORT_BYTE(port, value)	(outb( (hcf_8) (value), (hcf_io)(port) ))
#define OUT_PORT_WORD(port, value)	(outw((hcf_16) (value), (hcf_io)(port) ))

#define IN_PORT_STRING_16(port, dst, n)    insw((hcf_io)(port), dst, n)
#define OUT_PORT_STRING_16(port, src, n)   outsw((hcf_io)(port), src, n)
//#define IN_PORT_STRINGL(port, dst, n)   insl((port), (dst), (n))
//#define OUT_PORT_STRINGL(port, src, n)  outsl((port), (src), (n))
#define IN_PORT_STRING_32(port, dst, n)   insl((port), (dst), (n))
#define OUT_PORT_STRING_32(port, src, n)  outsl((port), (src), (n))
#define IN_PORT_HCF32(port)          inl( (hcf_io)(port) )
#define OUT_PORT_HCF32(port, value)  outl((hcf_32)(value), (hcf_io)(port) )

#define IN_PORT_DWORD(port)          IN_PORT_HCF32(port)
#define OUT_PORT_DWORD(port, value)  OUT_PORT_HCF32(port, value)

#define  IN_PORT_STRING_8_16(port, addr, len)	IN_PORT_STRING_16(port, addr, len)
#define  OUT_PORT_STRING_8_16(port, addr, len)	OUT_PORT_STRING_16(port, addr, len)

#ifndef CFG_SCAN_CHANNELS_2GHZ
#define CFG_SCAN_CHANNELS_2GHZ 0xFCC2
#endif /* CFG_SCAN_CHANNELS_2GHZ */

#define HCF_MAX_MSG 1600 //get going ;?
#endif	// WVLAN_49

/************************************************************************************************************/
/***********************************                                   **************************************/
/************************************************************************************************************/
#if ! defined 	HCF_ALIGN
#define 		HCF_ALIGN			1		//default to no alignment
#endif // 		HCF_ALIGN

#if ! defined 	HCF_ASSERT
#define 		HCF_ASSERT			0
#endif //		HCF_ASSERT

#if ! defined	HCF_BIG_ENDIAN
#define 		HCF_BIG_ENDIAN		0
#endif // 		HCF_BIG_ENDIAN

#if ! defined	HCF_DMA
#define 		HCF_DMA				0
#endif // 		HCF_DMA

#if ! defined	HCF_ENCAP
#define			HCF_ENCAP			HCF_ENC
#endif //		HCF_ENCAP

#if ! defined	HCF_EXT
#define			HCF_EXT				0
#endif //		HCF_EXT

#if ! defined	HCF_INT_ON
#define			HCF_INT_ON			1
#endif //		HCF_INT_ON

#if ! defined	HCF_IO
#define			HCF_IO				0		//default 16 bits support only, port I/O
#endif //		HCF_IO

#if ! defined	HCF_LEGACY
#define			HCF_LEGACY			0
#endif //		HCF_LEGACY

#if ! defined	HCF_MAX_LTV
#define 		HCF_MAX_LTV			1200	// sufficient for all known purposes
#endif //		HCF_MAX_LTV

#if ! defined	HCF_PROT_TIME
#define 		HCF_PROT_TIME		100		// number of 10K microsec protection timer against H/W malfunction
#endif // 		HCF_PROT_TIME

#if ! defined	HCF_SLEEP
#define			HCF_SLEEP			0
#endif //		HCF_SLEEP

#if ! defined	HCF_TALLIES
#define			HCF_TALLIES			( HCF_TALLIES_NIC | HCF_TALLIES_HCF )
#endif //		HCF_TALLIES

#if ! defined 	HCF_TYPE
#define 		HCF_TYPE 			0
#endif //		HCF_TYPE

#if				HCF_BIG_ENDIAN
#undef			HCF_BIG_ENDIAN
#define			HCF_BIG_ENDIAN		1		//just for convenience of generating cfg_hcf_opt
#endif //	 	HCF_BIG_ENDIAN

#if				HCF_DMA
#undef			HCF_DMA
#define			HCF_DMA				1		//just for convenience of generating cfg_hcf_opt
#endif //		HCF_DMA

#if				HCF_INT_ON
#undef			HCF_INT_ON
#define			HCF_INT_ON			1		//just for convenience of generating cfg_hcf_opt
#endif //		HCF_INT_ON


#if ! defined IN_PORT_STRING_8_16
#define  IN_PORT_STRING_8_16(port, addr, len)	IN_PORT_STRING_16(port, addr, len)
#define  OUT_PORT_STRING_8_16(port, addr, len)	OUT_PORT_STRING_16(port, addr, len)
#endif // IN_PORT_STRING_8_16

/************************************************************************************************/
/**********                                                                         *************/
/************************************************************************************************/

#if ! defined	FAR
#define 		FAR						// default to flat 32-bits code
#endif // 		FAR

typedef hcf_8  FAR *wci_bufp;			// segmented 16-bits or flat 32-bits pointer to 8 bits unit
typedef hcf_16 FAR *wci_recordp;		// segmented 16-bits or flat 32-bits pointer to 16 bits unit

/*	I/O Address size
*	Platforms which use port mapped I/O will (in general) have a 64k I/O space, conveniently expressed in a
*	16-bits quantity
*	Platforms which use memory mapped I/O will (in general) have an I/O space much larger than 64k, and need a
*	32-bits quantity to express the I/O base
*/

#if HCF_IO & HCF_IO_MEM
typedef hcf_32 hcf_io;
#else
typedef hcf_16 hcf_io;
#endif //HCF_IO

#if 	HCF_PROT_TIME > 128
#define HCF_PROT_TIME_SHFT	3
#define HCF_PROT_TIME_DIV	8
#elif 	HCF_PROT_TIME > 64
#define HCF_PROT_TIME_SHFT	2
#define HCF_PROT_TIME_DIV	4
#elif 	HCF_PROT_TIME > 32
#define HCF_PROT_TIME_SHFT	1
#define HCF_PROT_TIME_DIV	2
#else //HCF_PROT_TIME >= 19
#define HCF_PROT_TIME_SHFT	0
#define HCF_PROT_TIME_DIV	1
#endif

#define HCF_PROT_TIME_CNT (HCF_PROT_TIME / HCF_PROT_TIME_DIV)


/************************************************************************************************************/
/******************************************* . . . . . . . . .  *********************************************/
/************************************************************************************************************/

/* MSF_COMPONENT_ID is used to define the CFG_IDENTITY_STRCT in HCF.C
* CFG_IDENTITY_STRCT is defined in HCF.C purely based on convenience arguments.
* The HCF can not have the knowledge to determine the ComponentId field of the Identity record (aka as
* Version Record), therefore the MSF part of the Drivers must supply this value via the System Constant
* MSF_COMPONENT_ID.
* There is a set of values predefined in MDD.H (format COMP_ID_.....)
*
* Note that taking MSF_COMPONENT_ID as a default value for DUI_COMPAT_VAR is purely an implementation
* convenience, the numerical values of these two quantities have none functional relationship whatsoever.
*/

#if defined	MSF_COMPONENT_ID

#if ! defined	DUI_COMPAT_VAR
#define			DUI_COMPAT_VAR		MSF_COMPONENT_ID
#endif //		DUI_COMPAT_VAR

#if ! defined	DUI_COMPAT_BOT		//;?this way utilities can lower as well raise the bottom
#define			DUI_COMPAT_BOT		8
#endif //		DUI_COMPAT_BOT

#if ! defined	DUI_COMPAT_TOP		//;?this way utilities can lower as well raise the top
#define			DUI_COMPAT_TOP       8
#endif //		DUI_COMPAT_TOP

#endif // MSF_COMPONENT_ID

#if (HCF_TYPE) & HCF_TYPE_HII5

#if ! defined	HCF_HSI_VAR_5
#define			HCF_HSI_VAR_5
#endif //		HCF_HSI_VAR_5

#if ! defined	HCF_APF_VAR_4
#define			HCF_APF_VAR_4
#endif //		HCF_APF_VAR_4

#if (HCF_TYPE) & HCF_TYPE_WARP
#if ! defined	HCF_STA_VAR_4
#define			HCF_STA_VAR_4
#endif //		HCF_STA_VAR_4
#else
#if ! defined	HCF_STA_VAR_2
#define			HCF_STA_VAR_2
#endif //		HCF_STA_VAR_2
#endif

#if defined HCF_HSI_VAR_4
err: HSI variants 4 correspond with HII;
#endif // HCF_HSI_VAR_4

#else

#if ! defined	HCF_HSI_VAR_4
#define			HCF_HSI_VAR_4		//Hermes-II all types (for the time being!)
#endif //		HCF_HSI_VAR_4

#if ! defined	HCF_APF_VAR_2
#define 		HCF_APF_VAR_2
#endif //		HCF_APF_VAR_2

#if ! defined	HCF_STA_VAR_2
#define			HCF_STA_VAR_2
#endif //		HCF_STA_VAR_2

#endif // HCF_TYPE_HII5

#if ! defined	HCF_PRI_VAR_3
#define		HCF_PRI_VAR_3
#endif //		HCF_PRI_VAR_3

#if defined HCF_HSI_VAR_1 || defined HCF_HSI_VAR_2 || defined HCF_HSI_VAR_3
err: HSI variants 1, 2 and 3 correspond with H-I only;
#endif // HCF_HSI_VAR_1, HCF_HSI_VAR_2, HCF_HSI_VAR_3

#if defined HCF_PRI_VAR_1 || defined HCF_PRI_VAR_2
err: primary variants 1 and 2 correspond with H-I only;
#endif // HCF_PRI_VAR_1 / HCF_PRI_VAR_2


/************************************************************************************************************/
/******************************************* . . . . . . . . .  *********************************************/
/************************************************************************************************************/


/* The BASED customization macro is used to resolves the SS!=DS conflict for the Interrupt Service logic in
 * DOS Drivers. Due to the cumbersomeness of mixing C and assembler local BASED variables still end up in the
 * wrong segment. The workaround is that the HCF uses only global BASED variables or IFB-based variables.
 * The "BASED" construction (supposedly) only amounts to something in the small memory model.
 *
 * Note that the whole BASED rigmarole is needlessly complicated because both the Microsoft Compiler and
 * Linker are unnecessary restrictive in what far pointer manipulation they allow
 */

#if ! defined	BASED
#define 		BASED
#endif //		BASED

#if ! defined	EXTERN_C
#ifdef __cplusplus
#define			EXTERN_C extern "C"
#else
#define			EXTERN_C
#endif // __cplusplus
#endif //		EXTERN_C

#if ! defined	NULL
#define			NULL	((void *) 0)
#endif //		NULL

#if ! defined	TEXT
#define			TEXT(x)	x
#endif //		TEXT

/************************************************************************************************************/
/*********************** C O N F L I C T   D E T E C T I O N  &  R E S O L U T I O N ************************/
/************************************************************************************************************/
#if HCF_ALIGN != 1 && HCF_ALIGN != 2 && HCF_ALIGN != 4 && HCF_ALIGN != 8
err: invalid value for HCF_ALIGN;
#endif // HCF_ALIGN

#if (HCF_ASSERT) & ~( HCF_ASSERT_PRINTF | HCF_ASSERT_SW_SUP | HCF_ASSERT_MB | HCF_ASSERT_RT_MSF_RTN | \
					  HCF_ASSERT_LNK_MSF_RTN )
err: invalid value for HCF_ASSERT;
#endif // HCF_ASSERT

#if (HCF_ASSERT) & HCF_ASSERT_MB && ! ( (HCF_EXT) & HCF_EXT_MB )		//detect potential conflict
err: these macros are not used consistently;
#endif // HCF_ASSERT_MB / HCF_EXT_MB

#if HCF_BIG_ENDIAN != 0 && HCF_BIG_ENDIAN != 1
err: invalid value for HCF_BIG_ENDIAN;
#endif // HCF_BIG_ENDIAN

#if HCF_DMA != 0 && HCF_DMA != 1
err: invalid value for HCF_DMA;
#endif // HCF_DMA

#if (HCF_ENCAP) & ~( HCF_ENC | HCF_ENC_SUP )
err: invalid value for HCF_ENCAP;
#endif // HCF_ENCAP

#if (HCF_EXT) & ~( HCF_EXT_INFO_LOG | HCF_EXT_INT_TX_EX | HCF_EXT_TALLIES_FW | HCF_EXT_TALLIES_HCF	| \
				   HCF_EXT_NIC_ACCESS | HCF_EXT_MB | HCF_EXT_INT_TICK | \
				   HCF_EXT_IFB_STRCT | HCF_EXT_DESC_STRCT | HCF_EXT_TX_CONT )
err: invalid value for HCF_EXT;
#endif // HCF_EXT

#if HCF_INT_ON != 0 && HCF_INT_ON != 1
err: invalid value for HCF_INT_ON;
#endif // HCF_INT_ON

#if (HCF_IO) & ~( HCF_IO_MEM | HCF_IO_32BITS )
err: invalid value for HCF_IO;
#endif // HCF_IO

#if HCF_LEGACY != 0 && HCF_LEGACY != 1
err: invalid value for HCF_LEGACY;
#endif // HCF_LEGACY

#if HCF_MAX_LTV < 16 || HCF_MAX_LTV > 2304
err: invalid value for HCF_MAX_LTV;
#endif // HCF_MAX_LTV

#if HCF_PROT_TIME != 0 && ( HCF_PROT_TIME < 19 || 256 < HCF_PROT_TIME )
err: below minimum .08 second required by Hermes or possibly above hcf_32 capacity;
#endif // HCF_PROT_TIME

#if (HCF_SLEEP) & ~( HCF_CDS | HCF_DDS )
err: invalid value for HCF_SLEEP;
#endif // HCF_SLEEP

#if (HCF_SLEEP) && ! (HCF_INT_ON)
err: these macros are not used consistently;
#endif // HCF_SLEEP / HCF_INT_ON

#if (HCF_SLEEP) && ! ( (HCF_EXT) & HCF_EXT_INT_TICK )
//;? err: these macros are not used consistently;
#endif // HCF_SLEEP / HCF_EXT_INT_TICK

#if (HCF_TALLIES) & ~( HCF_TALLIES_HCF | HCF_TALLIES_NIC | HCF_TALLIES_RESET ) || \
	(HCF_TALLIES) == HCF_TALLIES_RESET
err: invalid value for HCF_TALLIES;
#endif // HCF_TALLIES

#if (HCF_TYPE) & ~(HCF_TYPE_WPA | HCF_TYPE_USB | HCF_TYPE_PRELOADED | HCF_TYPE_HII5 | HCF_TYPE_WARP | \
		HCF_TYPE_CCX /* | HCF_TYPE_TX_DELAY */ )
err: invalid value for HCF_TYPE;
#endif //HCF_TYPE

#if (HCF_TYPE) & HCF_TYPE_WARP && (HCF_TYPE) & HCF_TYPE_WPA
err: at most 1 of these macros should be defined;
#endif //HCF_TYPE_WARP / HCF_TYPE_WPA

#endif //HCFCFG_H

