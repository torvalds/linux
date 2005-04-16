#define VESTA
#include "ppc_40x.h"
#
        .align 2
        .text
#
# added by linguohui
        .extern   initb_ebiu0, initb_config, hdw_init_finish
        .extern   initb_hsmc0, initb_hsmc1, initb_cache
# end added
       .globl    HdwInit
#
HdwInit:
#
#-----------------------------------------------------------------------*
# If we are not executing from the FLASH get out                        *
#-----------------------------------------------------------------------*
# SAW keep this or comment out a la Hawthorne?
# r3 contains NIP when used with Linux
#        rlwinm r28, r3, 8, 24, 31    # if MSB == 0xFF -> FLASH address
#        cmpwi  r28, 0xff
#        bne    locn01
#
#
#------------------------------------------------------------------------
# Init_cpu. Bank registers are setup for the IBM STB.
#------------------------------------------------------------------------
#
# Setup processor core clock to be driven off chip.  This is GPI4 bit
# twenty.  Setup Open Drain, Output Select, Three-State Control,  and
# Three-State Select registers.
#


        pb0pesr  =        0x054
        pb0pear  =        0x056

	mflr	r30

#-----------------------------------------------------------------------------
# Vectors will be at 0x1F000000
# Dummy Machine check handler just does RFI before true handler gets installed
#-----------------------------------------------------------------------------
#if 1  /* xuwentao added*/
#ifdef SDRAM16MB
         lis     r10,0x0000
	addi 	r10,r10,0x0000
#else
        lis      r10,0x1F00
	addi	r10,r10,0x0000
#endif

        mtspr   evpr,r10              #EVPR: 0x0 or 0x1f000000 depending
        isync                         # on SDRAM memory model used.

        lis     r10,0xFFFF                # clear PB0_PESR because some
        ori    r10,r10,0xFFFF            #  transitions from flash,changed by linguohui
        mtdcr   pb0pesr,r10               #  to load RAM image via RiscWatch
        lis     r10,0x0000                #  cause PB0_PESR machine checks
        mtdcr   pb0pear,r10
        addis   r10,r10,0x0000            # clear the
        mtxer   r10                       #           XER just in case...
#endif /* xuwentao*/

        bl      initb_ebiu0                      # init EBIU

        bl      initb_config                     # config PPC and board




#------------------------------------------------------------------------
# EVPR  setup moved to top of this function.
#------------------------------------------------------------------------
#
	mtlr	r30
	blr
        .end
