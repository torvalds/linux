/* bnx2x_reg.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2012 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * The registers description starts with the register Access type followed
 * by size in bits. For example [RW 32]. The access types are:
 * R  - Read only
 * RC - Clear on read
 * RW - Read/Write
 * ST - Statistics register (clear on read)
 * W  - Write only
 * WB - Wide bus register - the size is over 32 bits and it should be
 *      read/write in consecutive 32 bits accesses
 * WR - Write Clear (write 1 to clear the bit)
 *
 */
#ifndef BNX2X_REG_H
#define BNX2X_REG_H

#define ATC_ATC_INT_STS_REG_ADDRESS_ERROR			 (0x1<<0)
#define ATC_ATC_INT_STS_REG_ATC_GPA_MULTIPLE_HITS		 (0x1<<2)
#define ATC_ATC_INT_STS_REG_ATC_IREQ_LESS_THAN_STU		 (0x1<<5)
#define ATC_ATC_INT_STS_REG_ATC_RCPL_TO_EMPTY_CNT		 (0x1<<3)
#define ATC_ATC_INT_STS_REG_ATC_TCPL_ERROR			 (0x1<<4)
#define ATC_ATC_INT_STS_REG_ATC_TCPL_TO_NOT_PEND		 (0x1<<1)
/* [RW 1] Initiate the ATC array - reset all the valid bits */
#define ATC_REG_ATC_INIT_ARRAY					 0x1100b8
/* [R 1] ATC initalization done */
#define ATC_REG_ATC_INIT_DONE					 0x1100bc
/* [RC 6] Interrupt register #0 read clear */
#define ATC_REG_ATC_INT_STS_CLR					 0x1101c0
/* [RW 5] Parity mask register #0 read/write */
#define ATC_REG_ATC_PRTY_MASK					 0x1101d8
/* [RC 5] Parity register #0 read clear */
#define ATC_REG_ATC_PRTY_STS_CLR				 0x1101d0
/* [RW 19] Interrupt mask register #0 read/write */
#define BRB1_REG_BRB1_INT_MASK					 0x60128
/* [R 19] Interrupt register #0 read */
#define BRB1_REG_BRB1_INT_STS					 0x6011c
/* [RW 4] Parity mask register #0 read/write */
#define BRB1_REG_BRB1_PRTY_MASK 				 0x60138
/* [R 4] Parity register #0 read */
#define BRB1_REG_BRB1_PRTY_STS					 0x6012c
/* [RC 4] Parity register #0 read clear */
#define BRB1_REG_BRB1_PRTY_STS_CLR				 0x60130
/* [RW 10] At address BRB1_IND_FREE_LIST_PRS_CRDT initialize free head. At
 * address BRB1_IND_FREE_LIST_PRS_CRDT+1 initialize free tail. At address
 * BRB1_IND_FREE_LIST_PRS_CRDT+2 initialize parser initial credit. Warning -
 * following reset the first rbc access to this reg must be write; there can
 * be no more rbc writes after the first one; there can be any number of rbc
 * read following the first write; rbc access not following these rules will
 * result in hang condition. */
#define BRB1_REG_FREE_LIST_PRS_CRDT				 0x60200
/* [RW 10] The number of free blocks below which the full signal to class 0
 * is asserted */
#define BRB1_REG_FULL_0_XOFF_THRESHOLD_0			 0x601d0
#define BRB1_REG_FULL_0_XOFF_THRESHOLD_1			 0x60230
/* [RW 11] The number of free blocks above which the full signal to class 0
 * is de-asserted */
#define BRB1_REG_FULL_0_XON_THRESHOLD_0				 0x601d4
#define BRB1_REG_FULL_0_XON_THRESHOLD_1				 0x60234
/* [RW 11] The number of free blocks below which the full signal to class 1
 * is asserted */
#define BRB1_REG_FULL_1_XOFF_THRESHOLD_0			 0x601d8
#define BRB1_REG_FULL_1_XOFF_THRESHOLD_1			 0x60238
/* [RW 11] The number of free blocks above which the full signal to class 1
 * is de-asserted */
#define BRB1_REG_FULL_1_XON_THRESHOLD_0				 0x601dc
#define BRB1_REG_FULL_1_XON_THRESHOLD_1				 0x6023c
/* [RW 11] The number of free blocks below which the full signal to the LB
 * port is asserted */
#define BRB1_REG_FULL_LB_XOFF_THRESHOLD				 0x601e0
/* [RW 10] The number of free blocks above which the full signal to the LB
 * port is de-asserted */
#define BRB1_REG_FULL_LB_XON_THRESHOLD				 0x601e4
/* [RW 10] The number of free blocks above which the High_llfc signal to
   interface #n is de-asserted. */
#define BRB1_REG_HIGH_LLFC_HIGH_THRESHOLD_0			 0x6014c
/* [RW 10] The number of free blocks below which the High_llfc signal to
   interface #n is asserted. */
#define BRB1_REG_HIGH_LLFC_LOW_THRESHOLD_0			 0x6013c
/* [RW 11] The number of blocks guarantied for the LB port */
#define BRB1_REG_LB_GUARANTIED					 0x601ec
/* [RW 11] The hysteresis on the guarantied buffer space for the Lb port
 * before signaling XON. */
#define BRB1_REG_LB_GUARANTIED_HYST				 0x60264
/* [RW 24] LL RAM data. */
#define BRB1_REG_LL_RAM						 0x61000
/* [RW 10] The number of free blocks above which the Low_llfc signal to
   interface #n is de-asserted. */
#define BRB1_REG_LOW_LLFC_HIGH_THRESHOLD_0			 0x6016c
/* [RW 10] The number of free blocks below which the Low_llfc signal to
   interface #n is asserted. */
#define BRB1_REG_LOW_LLFC_LOW_THRESHOLD_0			 0x6015c
/* [RW 11] The number of blocks guarantied for class 0 in MAC 0. The
 * register is applicable only when per_class_guaranty_mode is set. */
#define BRB1_REG_MAC_0_CLASS_0_GUARANTIED			 0x60244
/* [RW 11] The hysteresis on the guarantied buffer space for class 0 in MAC
 * 1 before signaling XON. The register is applicable only when
 * per_class_guaranty_mode is set. */
#define BRB1_REG_MAC_0_CLASS_0_GUARANTIED_HYST			 0x60254
/* [RW 11] The number of blocks guarantied for class 1 in MAC 0. The
 * register is applicable only when per_class_guaranty_mode is set. */
#define BRB1_REG_MAC_0_CLASS_1_GUARANTIED			 0x60248
/* [RW 11] The hysteresis on the guarantied buffer space for class 1in MAC 0
 * before signaling XON. The register is applicable only when
 * per_class_guaranty_mode is set. */
#define BRB1_REG_MAC_0_CLASS_1_GUARANTIED_HYST			 0x60258
/* [RW 11] The number of blocks guarantied for class 0in MAC1.The register
 * is applicable only when per_class_guaranty_mode is set. */
#define BRB1_REG_MAC_1_CLASS_0_GUARANTIED			 0x6024c
/* [RW 11] The hysteresis on the guarantied buffer space for class 0 in MAC
 * 1 before signaling XON. The register is applicable only when
 * per_class_guaranty_mode is set. */
#define BRB1_REG_MAC_1_CLASS_0_GUARANTIED_HYST			 0x6025c
/* [RW 11] The number of blocks guarantied for class 1 in MAC 1. The
 * register is applicable only when per_class_guaranty_mode is set. */
#define BRB1_REG_MAC_1_CLASS_1_GUARANTIED			 0x60250
/* [RW 11] The hysteresis on the guarantied buffer space for class 1 in MAC
 * 1 before signaling XON. The register is applicable only when
 * per_class_guaranty_mode is set. */
#define BRB1_REG_MAC_1_CLASS_1_GUARANTIED_HYST			 0x60260
/* [RW 11] The number of blocks guarantied for the MAC port. The register is
 * applicable only when per_class_guaranty_mode is reset. */
#define BRB1_REG_MAC_GUARANTIED_0				 0x601e8
#define BRB1_REG_MAC_GUARANTIED_1				 0x60240
/* [R 24] The number of full blocks. */
#define BRB1_REG_NUM_OF_FULL_BLOCKS				 0x60090
/* [ST 32] The number of cycles that the write_full signal towards MAC #0
   was asserted. */
#define BRB1_REG_NUM_OF_FULL_CYCLES_0				 0x600c8
#define BRB1_REG_NUM_OF_FULL_CYCLES_1				 0x600cc
#define BRB1_REG_NUM_OF_FULL_CYCLES_4				 0x600d8
/* [ST 32] The number of cycles that the pause signal towards MAC #0 was
   asserted. */
#define BRB1_REG_NUM_OF_PAUSE_CYCLES_0				 0x600b8
#define BRB1_REG_NUM_OF_PAUSE_CYCLES_1				 0x600bc
/* [RW 10] The number of free blocks below which the pause signal to class 0
 * is asserted */
#define BRB1_REG_PAUSE_0_XOFF_THRESHOLD_0			 0x601c0
#define BRB1_REG_PAUSE_0_XOFF_THRESHOLD_1			 0x60220
/* [RW 11] The number of free blocks above which the pause signal to class 0
 * is de-asserted */
#define BRB1_REG_PAUSE_0_XON_THRESHOLD_0			 0x601c4
#define BRB1_REG_PAUSE_0_XON_THRESHOLD_1			 0x60224
/* [RW 11] The number of free blocks below which the pause signal to class 1
 * is asserted */
#define BRB1_REG_PAUSE_1_XOFF_THRESHOLD_0			 0x601c8
#define BRB1_REG_PAUSE_1_XOFF_THRESHOLD_1			 0x60228
/* [RW 11] The number of free blocks above which the pause signal to class 1
 * is de-asserted */
#define BRB1_REG_PAUSE_1_XON_THRESHOLD_0			 0x601cc
#define BRB1_REG_PAUSE_1_XON_THRESHOLD_1			 0x6022c
/* [RW 10] Write client 0: De-assert pause threshold. Not Functional */
#define BRB1_REG_PAUSE_HIGH_THRESHOLD_0 			 0x60078
#define BRB1_REG_PAUSE_HIGH_THRESHOLD_1 			 0x6007c
/* [RW 10] Write client 0: Assert pause threshold. */
#define BRB1_REG_PAUSE_LOW_THRESHOLD_0				 0x60068
/* [RW 1] Indicates if to use per-class guaranty mode (new mode) or per-MAC
 * guaranty mode (backwards-compatible mode). 0=per-MAC guaranty mode (BC
 * mode). 1=per-class guaranty mode (new mode). */
#define BRB1_REG_PER_CLASS_GUARANTY_MODE			 0x60268
/* [R 24] The number of full blocks occpied by port. */
#define BRB1_REG_PORT_NUM_OCC_BLOCKS_0				 0x60094
/* [RW 1] Reset the design by software. */
#define BRB1_REG_SOFT_RESET					 0x600dc
/* [R 5] Used to read the value of the XX protection CAM occupancy counter. */
#define CCM_REG_CAM_OCCUP					 0xd0188
/* [RW 1] CM - CFC Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define CCM_REG_CCM_CFC_IFEN					 0xd003c
/* [RW 1] CM - QM Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define CCM_REG_CCM_CQM_IFEN					 0xd000c
/* [RW 1] If set the Q index; received from the QM is inserted to event ID.
   Otherwise 0 is inserted. */
#define CCM_REG_CCM_CQM_USE_Q					 0xd00c0
/* [RW 11] Interrupt mask register #0 read/write */
#define CCM_REG_CCM_INT_MASK					 0xd01e4
/* [R 11] Interrupt register #0 read */
#define CCM_REG_CCM_INT_STS					 0xd01d8
/* [RW 27] Parity mask register #0 read/write */
#define CCM_REG_CCM_PRTY_MASK					 0xd01f4
/* [R 27] Parity register #0 read */
#define CCM_REG_CCM_PRTY_STS					 0xd01e8
/* [RC 27] Parity register #0 read clear */
#define CCM_REG_CCM_PRTY_STS_CLR				 0xd01ec
/* [RW 3] The size of AG context region 0 in REG-pairs. Designates the MS
   REG-pair number (e.g. if region 0 is 6 REG-pairs; the value should be 5).
   Is used to determine the number of the AG context REG-pairs written back;
   when the input message Reg1WbFlg isn't set. */
#define CCM_REG_CCM_REG0_SZ					 0xd00c4
/* [RW 1] CM - STORM 0 Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define CCM_REG_CCM_STORM0_IFEN 				 0xd0004
/* [RW 1] CM - STORM 1 Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define CCM_REG_CCM_STORM1_IFEN 				 0xd0008
/* [RW 1] CDU AG read Interface enable. If 0 - the request input is
   disregarded; valid output is deasserted; all other signals are treated as
   usual; if 1 - normal activity. */
#define CCM_REG_CDU_AG_RD_IFEN					 0xd0030
/* [RW 1] CDU AG write Interface enable. If 0 - the request and valid input
   are disregarded; all other signals are treated as usual; if 1 - normal
   activity. */
#define CCM_REG_CDU_AG_WR_IFEN					 0xd002c
/* [RW 1] CDU STORM read Interface enable. If 0 - the request input is
   disregarded; valid output is deasserted; all other signals are treated as
   usual; if 1 - normal activity. */
#define CCM_REG_CDU_SM_RD_IFEN					 0xd0038
/* [RW 1] CDU STORM write Interface enable. If 0 - the request and valid
   input is disregarded; all other signals are treated as usual; if 1 -
   normal activity. */
#define CCM_REG_CDU_SM_WR_IFEN					 0xd0034
/* [RW 4] CFC output initial credit. Max credit available - 15.Write writes
   the initial credit value; read returns the current value of the credit
   counter. Must be initialized to 1 at start-up. */
#define CCM_REG_CFC_INIT_CRD					 0xd0204
/* [RW 2] Auxiliary counter flag Q number 1. */
#define CCM_REG_CNT_AUX1_Q					 0xd00c8
/* [RW 2] Auxiliary counter flag Q number 2. */
#define CCM_REG_CNT_AUX2_Q					 0xd00cc
/* [RW 28] The CM header value for QM request (primary). */
#define CCM_REG_CQM_CCM_HDR_P					 0xd008c
/* [RW 28] The CM header value for QM request (secondary). */
#define CCM_REG_CQM_CCM_HDR_S					 0xd0090
/* [RW 1] QM - CM Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define CCM_REG_CQM_CCM_IFEN					 0xd0014
/* [RW 6] QM output initial credit. Max credit available - 32. Write writes
   the initial credit value; read returns the current value of the credit
   counter. Must be initialized to 32 at start-up. */
#define CCM_REG_CQM_INIT_CRD					 0xd020c
/* [RW 3] The weight of the QM (primary) input in the WRR mechanism. 0
   stands for weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define CCM_REG_CQM_P_WEIGHT					 0xd00b8
/* [RW 3] The weight of the QM (secondary) input in the WRR mechanism. 0
   stands for weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define CCM_REG_CQM_S_WEIGHT					 0xd00bc
/* [RW 1] Input SDM Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define CCM_REG_CSDM_IFEN					 0xd0018
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the SDM interface is detected. */
#define CCM_REG_CSDM_LENGTH_MIS 				 0xd0170
/* [RW 3] The weight of the SDM input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define CCM_REG_CSDM_WEIGHT					 0xd00b4
/* [RW 28] The CM header for QM formatting in case of an error in the QM
   inputs. */
#define CCM_REG_ERR_CCM_HDR					 0xd0094
/* [RW 8] The Event ID in case the input message ErrorFlg is set. */
#define CCM_REG_ERR_EVNT_ID					 0xd0098
/* [RW 8] FIC0 output initial credit. Max credit available - 255. Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 64 at start-up. */
#define CCM_REG_FIC0_INIT_CRD					 0xd0210
/* [RW 8] FIC1 output initial credit. Max credit available - 255.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 64 at start-up. */
#define CCM_REG_FIC1_INIT_CRD					 0xd0214
/* [RW 1] Arbitration between Input Arbiter groups: 0 - fair Round-Robin; 1
   - strict priority defined by ~ccm_registers_gr_ag_pr.gr_ag_pr;
   ~ccm_registers_gr_ld0_pr.gr_ld0_pr and
   ~ccm_registers_gr_ld1_pr.gr_ld1_pr. Groups are according to channels and
   outputs to STORM: aggregation; load FIC0; load FIC1 and store. */
#define CCM_REG_GR_ARB_TYPE					 0xd015c
/* [RW 2] Load (FIC0) channel group priority. The lowest priority is 0; the
   highest priority is 3. It is supposed; that the Store channel priority is
   the compliment to 4 of the rest priorities - Aggregation channel; Load
   (FIC0) channel and Load (FIC1). */
#define CCM_REG_GR_LD0_PR					 0xd0164
/* [RW 2] Load (FIC1) channel group priority. The lowest priority is 0; the
   highest priority is 3. It is supposed; that the Store channel priority is
   the compliment to 4 of the rest priorities - Aggregation channel; Load
   (FIC0) channel and Load (FIC1). */
#define CCM_REG_GR_LD1_PR					 0xd0168
/* [RW 2] General flags index. */
#define CCM_REG_INV_DONE_Q					 0xd0108
/* [RW 4] The number of double REG-pairs(128 bits); loaded from the STORM
   context and sent to STORM; for a specific connection type. The double
   REG-pairs are used in order to align to STORM context row size of 128
   bits. The offset of these data in the STORM context is always 0. Index
   _(0..15) stands for the connection type (one of 16). */
#define CCM_REG_N_SM_CTX_LD_0					 0xd004c
#define CCM_REG_N_SM_CTX_LD_1					 0xd0050
#define CCM_REG_N_SM_CTX_LD_2					 0xd0054
#define CCM_REG_N_SM_CTX_LD_3					 0xd0058
#define CCM_REG_N_SM_CTX_LD_4					 0xd005c
/* [RW 1] Input pbf Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define CCM_REG_PBF_IFEN					 0xd0028
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the pbf interface is detected. */
#define CCM_REG_PBF_LENGTH_MIS					 0xd0180
/* [RW 3] The weight of the input pbf in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define CCM_REG_PBF_WEIGHT					 0xd00ac
#define CCM_REG_PHYS_QNUM1_0					 0xd0134
#define CCM_REG_PHYS_QNUM1_1					 0xd0138
#define CCM_REG_PHYS_QNUM2_0					 0xd013c
#define CCM_REG_PHYS_QNUM2_1					 0xd0140
#define CCM_REG_PHYS_QNUM3_0					 0xd0144
#define CCM_REG_PHYS_QNUM3_1					 0xd0148
#define CCM_REG_QOS_PHYS_QNUM0_0				 0xd0114
#define CCM_REG_QOS_PHYS_QNUM0_1				 0xd0118
#define CCM_REG_QOS_PHYS_QNUM1_0				 0xd011c
#define CCM_REG_QOS_PHYS_QNUM1_1				 0xd0120
#define CCM_REG_QOS_PHYS_QNUM2_0				 0xd0124
#define CCM_REG_QOS_PHYS_QNUM2_1				 0xd0128
#define CCM_REG_QOS_PHYS_QNUM3_0				 0xd012c
#define CCM_REG_QOS_PHYS_QNUM3_1				 0xd0130
/* [RW 1] STORM - CM Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define CCM_REG_STORM_CCM_IFEN					 0xd0010
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the STORM interface is detected. */
#define CCM_REG_STORM_LENGTH_MIS				 0xd016c
/* [RW 3] The weight of the STORM input in the WRR (Weighted Round robin)
   mechanism. 0 stands for weight 8 (the most prioritised); 1 stands for
   weight 1(least prioritised); 2 stands for weight 2 (more prioritised);
   tc. */
#define CCM_REG_STORM_WEIGHT					 0xd009c
/* [RW 1] Input tsem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define CCM_REG_TSEM_IFEN					 0xd001c
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the tsem interface is detected. */
#define CCM_REG_TSEM_LENGTH_MIS 				 0xd0174
/* [RW 3] The weight of the input tsem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define CCM_REG_TSEM_WEIGHT					 0xd00a0
/* [RW 1] Input usem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define CCM_REG_USEM_IFEN					 0xd0024
/* [RC 1] Set when message length mismatch (relative to last indication) at
   the usem interface is detected. */
#define CCM_REG_USEM_LENGTH_MIS 				 0xd017c
/* [RW 3] The weight of the input usem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define CCM_REG_USEM_WEIGHT					 0xd00a8
/* [RW 1] Input xsem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define CCM_REG_XSEM_IFEN					 0xd0020
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the xsem interface is detected. */
#define CCM_REG_XSEM_LENGTH_MIS 				 0xd0178
/* [RW 3] The weight of the input xsem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define CCM_REG_XSEM_WEIGHT					 0xd00a4
/* [RW 19] Indirect access to the descriptor table of the XX protection
   mechanism. The fields are: [5:0] - message length; [12:6] - message
   pointer; 18:13] - next pointer. */
#define CCM_REG_XX_DESCR_TABLE					 0xd0300
#define CCM_REG_XX_DESCR_TABLE_SIZE				 24
/* [R 7] Used to read the value of XX protection Free counter. */
#define CCM_REG_XX_FREE 					 0xd0184
/* [RW 6] Initial value for the credit counter; responsible for fulfilling
   of the Input Stage XX protection buffer by the XX protection pending
   messages. Max credit available - 127. Write writes the initial credit
   value; read returns the current value of the credit counter. Must be
   initialized to maximum XX protected message size - 2 at start-up. */
#define CCM_REG_XX_INIT_CRD					 0xd0220
/* [RW 7] The maximum number of pending messages; which may be stored in XX
   protection. At read the ~ccm_registers_xx_free.xx_free counter is read.
   At write comprises the start value of the ~ccm_registers_xx_free.xx_free
   counter. */
#define CCM_REG_XX_MSG_NUM					 0xd0224
/* [RW 8] The Event ID; sent to the STORM in case of XX overflow. */
#define CCM_REG_XX_OVFL_EVNT_ID 				 0xd0044
/* [RW 18] Indirect access to the XX table of the XX protection mechanism.
   The fields are: [5:0] - tail pointer; 11:6] - Link List size; 17:12] -
   header pointer. */
#define CCM_REG_XX_TABLE					 0xd0280
#define CDU_REG_CDU_CHK_MASK0					 0x101000
#define CDU_REG_CDU_CHK_MASK1					 0x101004
#define CDU_REG_CDU_CONTROL0					 0x101008
#define CDU_REG_CDU_DEBUG					 0x101010
#define CDU_REG_CDU_GLOBAL_PARAMS				 0x101020
/* [RW 7] Interrupt mask register #0 read/write */
#define CDU_REG_CDU_INT_MASK					 0x10103c
/* [R 7] Interrupt register #0 read */
#define CDU_REG_CDU_INT_STS					 0x101030
/* [RW 5] Parity mask register #0 read/write */
#define CDU_REG_CDU_PRTY_MASK					 0x10104c
/* [R 5] Parity register #0 read */
#define CDU_REG_CDU_PRTY_STS					 0x101040
/* [RC 5] Parity register #0 read clear */
#define CDU_REG_CDU_PRTY_STS_CLR				 0x101044
/* [RC 32] logging of error data in case of a CDU load error:
   {expected_cid[15:0]; xpected_type[2:0]; xpected_region[2:0]; ctive_error;
   ype_error; ctual_active; ctual_compressed_context}; */
#define CDU_REG_ERROR_DATA					 0x101014
/* [WB 216] L1TT ram access. each entry has the following format :
   {mrege_regions[7:0]; ffset12[5:0]...offset0[5:0];
   ength12[5:0]...length0[5:0]; d12[3:0]...id0[3:0]} */
#define CDU_REG_L1TT						 0x101800
/* [WB 24] MATT ram access. each entry has the following
   format:{RegionLength[11:0]; egionOffset[11:0]} */
#define CDU_REG_MATT						 0x101100
/* [RW 1] when this bit is set the CDU operates in e1hmf mode */
#define CDU_REG_MF_MODE 					 0x101050
/* [R 1] indication the initializing the activity counter by the hardware
   was done. */
#define CFC_REG_AC_INIT_DONE					 0x104078
/* [RW 13] activity counter ram access */
#define CFC_REG_ACTIVITY_COUNTER				 0x104400
#define CFC_REG_ACTIVITY_COUNTER_SIZE				 256
/* [R 1] indication the initializing the cams by the hardware was done. */
#define CFC_REG_CAM_INIT_DONE					 0x10407c
/* [RW 2] Interrupt mask register #0 read/write */
#define CFC_REG_CFC_INT_MASK					 0x104108
/* [R 2] Interrupt register #0 read */
#define CFC_REG_CFC_INT_STS					 0x1040fc
/* [RC 2] Interrupt register #0 read clear */
#define CFC_REG_CFC_INT_STS_CLR 				 0x104100
/* [RW 4] Parity mask register #0 read/write */
#define CFC_REG_CFC_PRTY_MASK					 0x104118
/* [R 4] Parity register #0 read */
#define CFC_REG_CFC_PRTY_STS					 0x10410c
/* [RC 4] Parity register #0 read clear */
#define CFC_REG_CFC_PRTY_STS_CLR				 0x104110
/* [RW 21] CID cam access (21:1 - Data; alid - 0) */
#define CFC_REG_CID_CAM 					 0x104800
#define CFC_REG_CONTROL0					 0x104028
#define CFC_REG_DEBUG0						 0x104050
/* [RW 14] indicates per error (in #cfc_registers_cfc_error_vector.cfc_error
   vector) whether the cfc should be disabled upon it */
#define CFC_REG_DISABLE_ON_ERROR				 0x104044
/* [RC 14] CFC error vector. when the CFC detects an internal error it will
   set one of these bits. the bit description can be found in CFC
   specifications */
#define CFC_REG_ERROR_VECTOR					 0x10403c
/* [WB 93] LCID info ram access */
#define CFC_REG_INFO_RAM					 0x105000
#define CFC_REG_INFO_RAM_SIZE					 1024
#define CFC_REG_INIT_REG					 0x10404c
#define CFC_REG_INTERFACES					 0x104058
/* [RW 24] {weight_load_client7[2:0] to weight_load_client0[2:0]}. this
   field allows changing the priorities of the weighted-round-robin arbiter
   which selects which CFC load client should be served next */
#define CFC_REG_LCREQ_WEIGHTS					 0x104084
/* [RW 16] Link List ram access; data = {prev_lcid; ext_lcid} */
#define CFC_REG_LINK_LIST					 0x104c00
#define CFC_REG_LINK_LIST_SIZE					 256
/* [R 1] indication the initializing the link list by the hardware was done. */
#define CFC_REG_LL_INIT_DONE					 0x104074
/* [R 9] Number of allocated LCIDs which are at empty state */
#define CFC_REG_NUM_LCIDS_ALLOC 				 0x104020
/* [R 9] Number of Arriving LCIDs in Link List Block */
#define CFC_REG_NUM_LCIDS_ARRIVING				 0x104004
#define CFC_REG_NUM_LCIDS_INSIDE_PF				 0x104120
/* [R 9] Number of Leaving LCIDs in Link List Block */
#define CFC_REG_NUM_LCIDS_LEAVING				 0x104018
#define CFC_REG_WEAK_ENABLE_PF					 0x104124
/* [RW 8] The event id for aggregated interrupt 0 */
#define CSDM_REG_AGG_INT_EVENT_0				 0xc2038
#define CSDM_REG_AGG_INT_EVENT_10				 0xc2060
#define CSDM_REG_AGG_INT_EVENT_11				 0xc2064
#define CSDM_REG_AGG_INT_EVENT_12				 0xc2068
#define CSDM_REG_AGG_INT_EVENT_13				 0xc206c
#define CSDM_REG_AGG_INT_EVENT_14				 0xc2070
#define CSDM_REG_AGG_INT_EVENT_15				 0xc2074
#define CSDM_REG_AGG_INT_EVENT_16				 0xc2078
#define CSDM_REG_AGG_INT_EVENT_2				 0xc2040
#define CSDM_REG_AGG_INT_EVENT_3				 0xc2044
#define CSDM_REG_AGG_INT_EVENT_4				 0xc2048
#define CSDM_REG_AGG_INT_EVENT_5				 0xc204c
#define CSDM_REG_AGG_INT_EVENT_6				 0xc2050
#define CSDM_REG_AGG_INT_EVENT_7				 0xc2054
#define CSDM_REG_AGG_INT_EVENT_8				 0xc2058
#define CSDM_REG_AGG_INT_EVENT_9				 0xc205c
/* [RW 1] For each aggregated interrupt index whether the mode is normal (0)
   or auto-mask-mode (1) */
#define CSDM_REG_AGG_INT_MODE_10				 0xc21e0
#define CSDM_REG_AGG_INT_MODE_11				 0xc21e4
#define CSDM_REG_AGG_INT_MODE_12				 0xc21e8
#define CSDM_REG_AGG_INT_MODE_13				 0xc21ec
#define CSDM_REG_AGG_INT_MODE_14				 0xc21f0
#define CSDM_REG_AGG_INT_MODE_15				 0xc21f4
#define CSDM_REG_AGG_INT_MODE_16				 0xc21f8
#define CSDM_REG_AGG_INT_MODE_6 				 0xc21d0
#define CSDM_REG_AGG_INT_MODE_7 				 0xc21d4
#define CSDM_REG_AGG_INT_MODE_8 				 0xc21d8
#define CSDM_REG_AGG_INT_MODE_9 				 0xc21dc
/* [RW 13] The start address in the internal RAM for the cfc_rsp lcid */
#define CSDM_REG_CFC_RSP_START_ADDR				 0xc2008
/* [RW 16] The maximum value of the completion counter #0 */
#define CSDM_REG_CMP_COUNTER_MAX0				 0xc201c
/* [RW 16] The maximum value of the completion counter #1 */
#define CSDM_REG_CMP_COUNTER_MAX1				 0xc2020
/* [RW 16] The maximum value of the completion counter #2 */
#define CSDM_REG_CMP_COUNTER_MAX2				 0xc2024
/* [RW 16] The maximum value of the completion counter #3 */
#define CSDM_REG_CMP_COUNTER_MAX3				 0xc2028
/* [RW 13] The start address in the internal RAM for the completion
   counters. */
#define CSDM_REG_CMP_COUNTER_START_ADDR 			 0xc200c
/* [RW 32] Interrupt mask register #0 read/write */
#define CSDM_REG_CSDM_INT_MASK_0				 0xc229c
#define CSDM_REG_CSDM_INT_MASK_1				 0xc22ac
/* [R 32] Interrupt register #0 read */
#define CSDM_REG_CSDM_INT_STS_0 				 0xc2290
#define CSDM_REG_CSDM_INT_STS_1 				 0xc22a0
/* [RW 11] Parity mask register #0 read/write */
#define CSDM_REG_CSDM_PRTY_MASK 				 0xc22bc
/* [R 11] Parity register #0 read */
#define CSDM_REG_CSDM_PRTY_STS					 0xc22b0
/* [RC 11] Parity register #0 read clear */
#define CSDM_REG_CSDM_PRTY_STS_CLR				 0xc22b4
#define CSDM_REG_ENABLE_IN1					 0xc2238
#define CSDM_REG_ENABLE_IN2					 0xc223c
#define CSDM_REG_ENABLE_OUT1					 0xc2240
#define CSDM_REG_ENABLE_OUT2					 0xc2244
/* [RW 4] The initial number of messages that can be sent to the pxp control
   interface without receiving any ACK. */
#define CSDM_REG_INIT_CREDIT_PXP_CTRL				 0xc24bc
/* [ST 32] The number of ACK after placement messages received */
#define CSDM_REG_NUM_OF_ACK_AFTER_PLACE 			 0xc227c
/* [ST 32] The number of packet end messages received from the parser */
#define CSDM_REG_NUM_OF_PKT_END_MSG				 0xc2274
/* [ST 32] The number of requests received from the pxp async if */
#define CSDM_REG_NUM_OF_PXP_ASYNC_REQ				 0xc2278
/* [ST 32] The number of commands received in queue 0 */
#define CSDM_REG_NUM_OF_Q0_CMD					 0xc2248
/* [ST 32] The number of commands received in queue 10 */
#define CSDM_REG_NUM_OF_Q10_CMD 				 0xc226c
/* [ST 32] The number of commands received in queue 11 */
#define CSDM_REG_NUM_OF_Q11_CMD 				 0xc2270
/* [ST 32] The number of commands received in queue 1 */
#define CSDM_REG_NUM_OF_Q1_CMD					 0xc224c
/* [ST 32] The number of commands received in queue 3 */
#define CSDM_REG_NUM_OF_Q3_CMD					 0xc2250
/* [ST 32] The number of commands received in queue 4 */
#define CSDM_REG_NUM_OF_Q4_CMD					 0xc2254
/* [ST 32] The number of commands received in queue 5 */
#define CSDM_REG_NUM_OF_Q5_CMD					 0xc2258
/* [ST 32] The number of commands received in queue 6 */
#define CSDM_REG_NUM_OF_Q6_CMD					 0xc225c
/* [ST 32] The number of commands received in queue 7 */
#define CSDM_REG_NUM_OF_Q7_CMD					 0xc2260
/* [ST 32] The number of commands received in queue 8 */
#define CSDM_REG_NUM_OF_Q8_CMD					 0xc2264
/* [ST 32] The number of commands received in queue 9 */
#define CSDM_REG_NUM_OF_Q9_CMD					 0xc2268
/* [RW 13] The start address in the internal RAM for queue counters */
#define CSDM_REG_Q_COUNTER_START_ADDR				 0xc2010
/* [R 1] pxp_ctrl rd_data fifo empty in sdm_dma_rsp block */
#define CSDM_REG_RSP_PXP_CTRL_RDATA_EMPTY			 0xc2548
/* [R 1] parser fifo empty in sdm_sync block */
#define CSDM_REG_SYNC_PARSER_EMPTY				 0xc2550
/* [R 1] parser serial fifo empty in sdm_sync block */
#define CSDM_REG_SYNC_SYNC_EMPTY				 0xc2558
/* [RW 32] Tick for timer counter. Applicable only when
   ~csdm_registers_timer_tick_enable.timer_tick_enable =1 */
#define CSDM_REG_TIMER_TICK					 0xc2000
/* [RW 5] The number of time_slots in the arbitration cycle */
#define CSEM_REG_ARB_CYCLE_SIZE 				 0x200034
/* [RW 3] The source that is associated with arbitration element 0. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2 */
#define CSEM_REG_ARB_ELEMENT0					 0x200020
/* [RW 3] The source that is associated with arbitration element 1. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~csem_registers_arb_element0.arb_element0 */
#define CSEM_REG_ARB_ELEMENT1					 0x200024
/* [RW 3] The source that is associated with arbitration element 2. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~csem_registers_arb_element0.arb_element0
   and ~csem_registers_arb_element1.arb_element1 */
#define CSEM_REG_ARB_ELEMENT2					 0x200028
/* [RW 3] The source that is associated with arbitration element 3. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.Could
   not be equal to register ~csem_registers_arb_element0.arb_element0 and
   ~csem_registers_arb_element1.arb_element1 and
   ~csem_registers_arb_element2.arb_element2 */
#define CSEM_REG_ARB_ELEMENT3					 0x20002c
/* [RW 3] The source that is associated with arbitration element 4. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~csem_registers_arb_element0.arb_element0
   and ~csem_registers_arb_element1.arb_element1 and
   ~csem_registers_arb_element2.arb_element2 and
   ~csem_registers_arb_element3.arb_element3 */
#define CSEM_REG_ARB_ELEMENT4					 0x200030
/* [RW 32] Interrupt mask register #0 read/write */
#define CSEM_REG_CSEM_INT_MASK_0				 0x200110
#define CSEM_REG_CSEM_INT_MASK_1				 0x200120
/* [R 32] Interrupt register #0 read */
#define CSEM_REG_CSEM_INT_STS_0 				 0x200104
#define CSEM_REG_CSEM_INT_STS_1 				 0x200114
/* [RW 32] Parity mask register #0 read/write */
#define CSEM_REG_CSEM_PRTY_MASK_0				 0x200130
#define CSEM_REG_CSEM_PRTY_MASK_1				 0x200140
/* [R 32] Parity register #0 read */
#define CSEM_REG_CSEM_PRTY_STS_0				 0x200124
#define CSEM_REG_CSEM_PRTY_STS_1				 0x200134
/* [RC 32] Parity register #0 read clear */
#define CSEM_REG_CSEM_PRTY_STS_CLR_0				 0x200128
#define CSEM_REG_CSEM_PRTY_STS_CLR_1				 0x200138
#define CSEM_REG_ENABLE_IN					 0x2000a4
#define CSEM_REG_ENABLE_OUT					 0x2000a8
/* [RW 32] This address space contains all registers and memories that are
   placed in SEM_FAST block. The SEM_FAST registers are described in
   appendix B. In order to access the sem_fast registers the base address
   ~fast_memory.fast_memory should be added to eachsem_fast register offset. */
#define CSEM_REG_FAST_MEMORY					 0x220000
/* [RW 1] Disables input messages from FIC0 May be updated during run_time
   by the microcode */
#define CSEM_REG_FIC0_DISABLE					 0x200224
/* [RW 1] Disables input messages from FIC1 May be updated during run_time
   by the microcode */
#define CSEM_REG_FIC1_DISABLE					 0x200234
/* [RW 15] Interrupt table Read and write access to it is not possible in
   the middle of the work */
#define CSEM_REG_INT_TABLE					 0x200400
/* [ST 24] Statistics register. The number of messages that entered through
   FIC0 */
#define CSEM_REG_MSG_NUM_FIC0					 0x200000
/* [ST 24] Statistics register. The number of messages that entered through
   FIC1 */
#define CSEM_REG_MSG_NUM_FIC1					 0x200004
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC0 */
#define CSEM_REG_MSG_NUM_FOC0					 0x200008
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC1 */
#define CSEM_REG_MSG_NUM_FOC1					 0x20000c
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC2 */
#define CSEM_REG_MSG_NUM_FOC2					 0x200010
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC3 */
#define CSEM_REG_MSG_NUM_FOC3					 0x200014
/* [RW 1] Disables input messages from the passive buffer May be updated
   during run_time by the microcode */
#define CSEM_REG_PAS_DISABLE					 0x20024c
/* [WB 128] Debug only. Passive buffer memory */
#define CSEM_REG_PASSIVE_BUFFER 				 0x202000
/* [WB 46] pram memory. B45 is parity; b[44:0] - data. */
#define CSEM_REG_PRAM						 0x240000
/* [R 16] Valid sleeping threads indication have bit per thread */
#define CSEM_REG_SLEEP_THREADS_VALID				 0x20026c
/* [R 1] EXT_STORE FIFO is empty in sem_slow_ls_ext */
#define CSEM_REG_SLOW_EXT_STORE_EMPTY				 0x2002a0
/* [RW 16] List of free threads . There is a bit per thread. */
#define CSEM_REG_THREADS_LIST					 0x2002e4
/* [RW 3] The arbitration scheme of time_slot 0 */
#define CSEM_REG_TS_0_AS					 0x200038
/* [RW 3] The arbitration scheme of time_slot 10 */
#define CSEM_REG_TS_10_AS					 0x200060
/* [RW 3] The arbitration scheme of time_slot 11 */
#define CSEM_REG_TS_11_AS					 0x200064
/* [RW 3] The arbitration scheme of time_slot 12 */
#define CSEM_REG_TS_12_AS					 0x200068
/* [RW 3] The arbitration scheme of time_slot 13 */
#define CSEM_REG_TS_13_AS					 0x20006c
/* [RW 3] The arbitration scheme of time_slot 14 */
#define CSEM_REG_TS_14_AS					 0x200070
/* [RW 3] The arbitration scheme of time_slot 15 */
#define CSEM_REG_TS_15_AS					 0x200074
/* [RW 3] The arbitration scheme of time_slot 16 */
#define CSEM_REG_TS_16_AS					 0x200078
/* [RW 3] The arbitration scheme of time_slot 17 */
#define CSEM_REG_TS_17_AS					 0x20007c
/* [RW 3] The arbitration scheme of time_slot 18 */
#define CSEM_REG_TS_18_AS					 0x200080
/* [RW 3] The arbitration scheme of time_slot 1 */
#define CSEM_REG_TS_1_AS					 0x20003c
/* [RW 3] The arbitration scheme of time_slot 2 */
#define CSEM_REG_TS_2_AS					 0x200040
/* [RW 3] The arbitration scheme of time_slot 3 */
#define CSEM_REG_TS_3_AS					 0x200044
/* [RW 3] The arbitration scheme of time_slot 4 */
#define CSEM_REG_TS_4_AS					 0x200048
/* [RW 3] The arbitration scheme of time_slot 5 */
#define CSEM_REG_TS_5_AS					 0x20004c
/* [RW 3] The arbitration scheme of time_slot 6 */
#define CSEM_REG_TS_6_AS					 0x200050
/* [RW 3] The arbitration scheme of time_slot 7 */
#define CSEM_REG_TS_7_AS					 0x200054
/* [RW 3] The arbitration scheme of time_slot 8 */
#define CSEM_REG_TS_8_AS					 0x200058
/* [RW 3] The arbitration scheme of time_slot 9 */
#define CSEM_REG_TS_9_AS					 0x20005c
/* [W 7] VF or PF ID for reset error bit. Values 0-63 reset error bit for 64
 * VF; values 64-67 reset error for 4 PF; values 68-127 are not valid. */
#define CSEM_REG_VFPF_ERR_NUM					 0x200380
/* [RW 1] Parity mask register #0 read/write */
#define DBG_REG_DBG_PRTY_MASK					 0xc0a8
/* [R 1] Parity register #0 read */
#define DBG_REG_DBG_PRTY_STS					 0xc09c
/* [RC 1] Parity register #0 read clear */
#define DBG_REG_DBG_PRTY_STS_CLR				 0xc0a0
/* [RW 1] When set the DMAE will process the commands as in E1.5. 1.The
 * function that is used is always SRC-PCI; 2.VF_Valid = 0; 3.VFID=0;
 * 4.Completion function=0; 5.Error handling=0 */
#define DMAE_REG_BACKWARD_COMP_EN				 0x10207c
/* [RW 32] Commands memory. The address to command X; row Y is to calculated
   as 14*X+Y. */
#define DMAE_REG_CMD_MEM					 0x102400
#define DMAE_REG_CMD_MEM_SIZE					 224
/* [RW 1] If 0 - the CRC-16c initial value is all zeroes; if 1 - the CRC-16c
   initial value is all ones. */
#define DMAE_REG_CRC16C_INIT					 0x10201c
/* [RW 1] If 0 - the CRC-16 T10 initial value is all zeroes; if 1 - the
   CRC-16 T10 initial value is all ones. */
#define DMAE_REG_CRC16T10_INIT					 0x102020
/* [RW 2] Interrupt mask register #0 read/write */
#define DMAE_REG_DMAE_INT_MASK					 0x102054
/* [RW 4] Parity mask register #0 read/write */
#define DMAE_REG_DMAE_PRTY_MASK 				 0x102064
/* [R 4] Parity register #0 read */
#define DMAE_REG_DMAE_PRTY_STS					 0x102058
/* [RC 4] Parity register #0 read clear */
#define DMAE_REG_DMAE_PRTY_STS_CLR				 0x10205c
/* [RW 1] Command 0 go. */
#define DMAE_REG_GO_C0						 0x102080
/* [RW 1] Command 1 go. */
#define DMAE_REG_GO_C1						 0x102084
/* [RW 1] Command 10 go. */
#define DMAE_REG_GO_C10 					 0x102088
/* [RW 1] Command 11 go. */
#define DMAE_REG_GO_C11 					 0x10208c
/* [RW 1] Command 12 go. */
#define DMAE_REG_GO_C12 					 0x102090
/* [RW 1] Command 13 go. */
#define DMAE_REG_GO_C13 					 0x102094
/* [RW 1] Command 14 go. */
#define DMAE_REG_GO_C14 					 0x102098
/* [RW 1] Command 15 go. */
#define DMAE_REG_GO_C15 					 0x10209c
/* [RW 1] Command 2 go. */
#define DMAE_REG_GO_C2						 0x1020a0
/* [RW 1] Command 3 go. */
#define DMAE_REG_GO_C3						 0x1020a4
/* [RW 1] Command 4 go. */
#define DMAE_REG_GO_C4						 0x1020a8
/* [RW 1] Command 5 go. */
#define DMAE_REG_GO_C5						 0x1020ac
/* [RW 1] Command 6 go. */
#define DMAE_REG_GO_C6						 0x1020b0
/* [RW 1] Command 7 go. */
#define DMAE_REG_GO_C7						 0x1020b4
/* [RW 1] Command 8 go. */
#define DMAE_REG_GO_C8						 0x1020b8
/* [RW 1] Command 9 go. */
#define DMAE_REG_GO_C9						 0x1020bc
/* [RW 1] DMAE GRC Interface (Target; aster) enable. If 0 - the acknowledge
   input is disregarded; valid is deasserted; all other signals are treated
   as usual; if 1 - normal activity. */
#define DMAE_REG_GRC_IFEN					 0x102008
/* [RW 1] DMAE PCI Interface (Request; ead; rite) enable. If 0 - the
   acknowledge input is disregarded; valid is deasserted; full is asserted;
   all other signals are treated as usual; if 1 - normal activity. */
#define DMAE_REG_PCI_IFEN					 0x102004
/* [RW 4] DMAE- PCI Request Interface initial credit. Write writes the
   initial value to the credit counter; related to the address. Read returns
   the current value of the counter. */
#define DMAE_REG_PXP_REQ_INIT_CRD				 0x1020c0
/* [RW 8] Aggregation command. */
#define DORQ_REG_AGG_CMD0					 0x170060
/* [RW 8] Aggregation command. */
#define DORQ_REG_AGG_CMD1					 0x170064
/* [RW 8] Aggregation command. */
#define DORQ_REG_AGG_CMD2					 0x170068
/* [RW 8] Aggregation command. */
#define DORQ_REG_AGG_CMD3					 0x17006c
/* [RW 28] UCM Header. */
#define DORQ_REG_CMHEAD_RX					 0x170050
/* [RW 32] Doorbell address for RBC doorbells (function 0). */
#define DORQ_REG_DB_ADDR0					 0x17008c
/* [RW 5] Interrupt mask register #0 read/write */
#define DORQ_REG_DORQ_INT_MASK					 0x170180
/* [R 5] Interrupt register #0 read */
#define DORQ_REG_DORQ_INT_STS					 0x170174
/* [RC 5] Interrupt register #0 read clear */
#define DORQ_REG_DORQ_INT_STS_CLR				 0x170178
/* [RW 2] Parity mask register #0 read/write */
#define DORQ_REG_DORQ_PRTY_MASK 				 0x170190
/* [R 2] Parity register #0 read */
#define DORQ_REG_DORQ_PRTY_STS					 0x170184
/* [RC 2] Parity register #0 read clear */
#define DORQ_REG_DORQ_PRTY_STS_CLR				 0x170188
/* [RW 8] The address to write the DPM CID to STORM. */
#define DORQ_REG_DPM_CID_ADDR					 0x170044
/* [RW 5] The DPM mode CID extraction offset. */
#define DORQ_REG_DPM_CID_OFST					 0x170030
/* [RW 12] The threshold of the DQ FIFO to send the almost full interrupt. */
#define DORQ_REG_DQ_FIFO_AFULL_TH				 0x17007c
/* [RW 12] The threshold of the DQ FIFO to send the full interrupt. */
#define DORQ_REG_DQ_FIFO_FULL_TH				 0x170078
/* [R 13] Current value of the DQ FIFO fill level according to following
   pointer. The range is 0 - 256 FIFO rows; where each row stands for the
   doorbell. */
#define DORQ_REG_DQ_FILL_LVLF					 0x1700a4
/* [R 1] DQ FIFO full status. Is set; when FIFO filling level is more or
   equal to full threshold; reset on full clear. */
#define DORQ_REG_DQ_FULL_ST					 0x1700c0
/* [RW 28] The value sent to CM header in the case of CFC load error. */
#define DORQ_REG_ERR_CMHEAD					 0x170058
#define DORQ_REG_IF_EN						 0x170004
#define DORQ_REG_MODE_ACT					 0x170008
/* [RW 5] The normal mode CID extraction offset. */
#define DORQ_REG_NORM_CID_OFST					 0x17002c
/* [RW 28] TCM Header when only TCP context is loaded. */
#define DORQ_REG_NORM_CMHEAD_TX 				 0x17004c
/* [RW 3] The number of simultaneous outstanding requests to Context Fetch
   Interface. */
#define DORQ_REG_OUTST_REQ					 0x17003c
#define DORQ_REG_PF_USAGE_CNT					 0x1701d0
#define DORQ_REG_REGN						 0x170038
/* [R 4] Current value of response A counter credit. Initial credit is
   configured through write to ~dorq_registers_rsp_init_crd.rsp_init_crd
   register. */
#define DORQ_REG_RSPA_CRD_CNT					 0x1700ac
/* [R 4] Current value of response B counter credit. Initial credit is
   configured through write to ~dorq_registers_rsp_init_crd.rsp_init_crd
   register. */
#define DORQ_REG_RSPB_CRD_CNT					 0x1700b0
/* [RW 4] The initial credit at the Doorbell Response Interface. The write
   writes the same initial credit to the rspa_crd_cnt and rspb_crd_cnt. The
   read reads this written value. */
#define DORQ_REG_RSP_INIT_CRD					 0x170048
/* [RW 4] Initial activity counter value on the load request; when the
   shortcut is done. */
#define DORQ_REG_SHRT_ACT_CNT					 0x170070
/* [RW 28] TCM Header when both ULP and TCP context is loaded. */
#define DORQ_REG_SHRT_CMHEAD					 0x170054
#define HC_CONFIG_0_REG_ATTN_BIT_EN_0				 (0x1<<4)
#define HC_CONFIG_0_REG_BLOCK_DISABLE_0				 (0x1<<0)
#define HC_CONFIG_0_REG_INT_LINE_EN_0				 (0x1<<3)
#define HC_CONFIG_0_REG_MSI_ATTN_EN_0				 (0x1<<7)
#define HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0			 (0x1<<2)
#define HC_CONFIG_0_REG_SINGLE_ISR_EN_0				 (0x1<<1)
#define HC_CONFIG_1_REG_BLOCK_DISABLE_1				 (0x1<<0)
#define HC_REG_AGG_INT_0					 0x108050
#define HC_REG_AGG_INT_1					 0x108054
#define HC_REG_ATTN_BIT 					 0x108120
#define HC_REG_ATTN_IDX 					 0x108100
#define HC_REG_ATTN_MSG0_ADDR_L 				 0x108018
#define HC_REG_ATTN_MSG1_ADDR_L 				 0x108020
#define HC_REG_ATTN_NUM_P0					 0x108038
#define HC_REG_ATTN_NUM_P1					 0x10803c
#define HC_REG_COMMAND_REG					 0x108180
#define HC_REG_CONFIG_0 					 0x108000
#define HC_REG_CONFIG_1 					 0x108004
#define HC_REG_FUNC_NUM_P0					 0x1080ac
#define HC_REG_FUNC_NUM_P1					 0x1080b0
/* [RW 3] Parity mask register #0 read/write */
#define HC_REG_HC_PRTY_MASK					 0x1080a0
/* [R 3] Parity register #0 read */
#define HC_REG_HC_PRTY_STS					 0x108094
/* [RC 3] Parity register #0 read clear */
#define HC_REG_HC_PRTY_STS_CLR					 0x108098
#define HC_REG_INT_MASK						 0x108108
#define HC_REG_LEADING_EDGE_0					 0x108040
#define HC_REG_LEADING_EDGE_1					 0x108048
#define HC_REG_MAIN_MEMORY					 0x108800
#define HC_REG_MAIN_MEMORY_SIZE					 152
#define HC_REG_P0_PROD_CONS					 0x108200
#define HC_REG_P1_PROD_CONS					 0x108400
#define HC_REG_PBA_COMMAND					 0x108140
#define HC_REG_PCI_CONFIG_0					 0x108010
#define HC_REG_PCI_CONFIG_1					 0x108014
#define HC_REG_STATISTIC_COUNTERS				 0x109000
#define HC_REG_TRAILING_EDGE_0					 0x108044
#define HC_REG_TRAILING_EDGE_1					 0x10804c
#define HC_REG_UC_RAM_ADDR_0					 0x108028
#define HC_REG_UC_RAM_ADDR_1					 0x108030
#define HC_REG_USTORM_ADDR_FOR_COALESCE 			 0x108068
#define HC_REG_VQID_0						 0x108008
#define HC_REG_VQID_1						 0x10800c
#define IGU_BLOCK_CONFIGURATION_REG_BACKWARD_COMP_EN		 (0x1<<1)
#define IGU_BLOCK_CONFIGURATION_REG_BLOCK_ENABLE		 (0x1<<0)
#define IGU_REG_ATTENTION_ACK_BITS				 0x130108
/* [R 4] Debug: attn_fsm */
#define IGU_REG_ATTN_FSM					 0x130054
#define IGU_REG_ATTN_MSG_ADDR_H				 0x13011c
#define IGU_REG_ATTN_MSG_ADDR_L				 0x130120
/* [R 4] Debug: [3] - attention write done message is pending (0-no pending;
 * 1-pending). [2:0] = PFID. Pending means attention message was sent; but
 * write done didn't receive. */
#define IGU_REG_ATTN_WRITE_DONE_PENDING			 0x130030
#define IGU_REG_BLOCK_CONFIGURATION				 0x130000
#define IGU_REG_COMMAND_REG_32LSB_DATA				 0x130124
#define IGU_REG_COMMAND_REG_CTRL				 0x13012c
/* [WB_R 32] Cleanup bit status per SB. 1 = cleanup is set. 0 = cleanup bit
 * is clear. The bits in this registers are set and clear via the producer
 * command. Data valid only in addresses 0-4. all the rest are zero. */
#define IGU_REG_CSTORM_TYPE_0_SB_CLEANUP			 0x130200
/* [R 5] Debug: ctrl_fsm */
#define IGU_REG_CTRL_FSM					 0x130064
/* [R 1] data available for error memory. If this bit is clear do not red
 * from error_handling_memory. */
#define IGU_REG_ERROR_HANDLING_DATA_VALID			 0x130130
/* [RW 11] Parity mask register #0 read/write */
#define IGU_REG_IGU_PRTY_MASK					 0x1300a8
/* [R 11] Parity register #0 read */
#define IGU_REG_IGU_PRTY_STS					 0x13009c
/* [RC 11] Parity register #0 read clear */
#define IGU_REG_IGU_PRTY_STS_CLR				 0x1300a0
/* [R 4] Debug: int_handle_fsm */
#define IGU_REG_INT_HANDLE_FSM					 0x130050
#define IGU_REG_LEADING_EDGE_LATCH				 0x130134
/* [RW 14] mapping CAM; relevant for E2 operating mode only. [0] - valid.
 * [6:1] - vector number; [13:7] - FID (if VF - [13] = 0; [12:7] = VF
 * number; if PF - [13] = 1; [12:10] = 0; [9:7] = PF number); */
#define IGU_REG_MAPPING_MEMORY					 0x131000
#define IGU_REG_MAPPING_MEMORY_SIZE				 136
#define IGU_REG_PBA_STATUS_LSB					 0x130138
#define IGU_REG_PBA_STATUS_MSB					 0x13013c
#define IGU_REG_PCI_PF_MSI_EN					 0x130140
#define IGU_REG_PCI_PF_MSIX_EN					 0x130144
#define IGU_REG_PCI_PF_MSIX_FUNC_MASK				 0x130148
/* [WB_R 32] Each bit represent the pending bits status for that SB. 0 = no
 * pending; 1 = pending. Pendings means interrupt was asserted; and write
 * done was not received. Data valid only in addresses 0-4. all the rest are
 * zero. */
#define IGU_REG_PENDING_BITS_STATUS				 0x130300
#define IGU_REG_PF_CONFIGURATION				 0x130154
/* [RW 20] producers only. E2 mode: address 0-135 match to the mapping
 * memory; 136 - PF0 default prod; 137 PF1 default prod; 138 - PF2 default
 * prod; 139 PF3 default prod; 140 - PF0 - ATTN prod; 141 - PF1 - ATTN prod;
 * 142 - PF2 - ATTN prod; 143 - PF3 - ATTN prod; 144-147 reserved. E1.5 mode
 * - In backward compatible mode; for non default SB; each even line in the
 * memory holds the U producer and each odd line hold the C producer. The
 * first 128 producer are for NDSB (PF0 - 0-31; PF1 - 32-63 and so on). The
 * last 20 producers are for the DSB for each PF. each PF has five segments
 * (the order inside each segment is PF0; PF1; PF2; PF3) - 128-131 U prods;
 * 132-135 C prods; 136-139 X prods; 140-143 T prods; 144-147 ATTN prods; */
#define IGU_REG_PROD_CONS_MEMORY				 0x132000
/* [R 3] Debug: pxp_arb_fsm */
#define IGU_REG_PXP_ARB_FSM					 0x130068
/* [RW 6] Write one for each bit will reset the appropriate memory. When the
 * memory reset finished the appropriate bit will be clear. Bit 0 - mapping
 * memory; Bit 1 - SB memory; Bit 2 - SB interrupt and mask register; Bit 3
 * - MSIX memory; Bit 4 - PBA memory; Bit 5 - statistics; */
#define IGU_REG_RESET_MEMORIES					 0x130158
/* [R 4] Debug: sb_ctrl_fsm */
#define IGU_REG_SB_CTRL_FSM					 0x13004c
#define IGU_REG_SB_INT_BEFORE_MASK_LSB				 0x13015c
#define IGU_REG_SB_INT_BEFORE_MASK_MSB				 0x130160
#define IGU_REG_SB_MASK_LSB					 0x130164
#define IGU_REG_SB_MASK_MSB					 0x130168
/* [RW 16] Number of command that were dropped without causing an interrupt
 * due to: read access for WO BAR address; or write access for RO BAR
 * address or any access for reserved address or PCI function error is set
 * and address is not MSIX; PBA or cleanup */
#define IGU_REG_SILENT_DROP					 0x13016c
/* [RW 10] Number of MSI/MSIX/ATTN messages sent for the function: 0-63 -
 * number of MSIX messages per VF; 64-67 - number of MSI/MSIX messages per
 * PF; 68-71 number of ATTN messages per PF */
#define IGU_REG_STATISTIC_NUM_MESSAGE_SENT			 0x130800
/* [RW 32] Number of cycles the timer mask masking the IGU interrupt when a
 * timer mask command arrives. Value must be bigger than 100. */
#define IGU_REG_TIMER_MASKING_VALUE				 0x13003c
#define IGU_REG_TRAILING_EDGE_LATCH				 0x130104
#define IGU_REG_VF_CONFIGURATION				 0x130170
/* [WB_R 32] Each bit represent write done pending bits status for that SB
 * (MSI/MSIX message was sent and write done was not received yet). 0 =
 * clear; 1 = set. Data valid only in addresses 0-4. all the rest are zero. */
#define IGU_REG_WRITE_DONE_PENDING				 0x130480
#define MCP_A_REG_MCPR_SCRATCH					 0x3a0000
#define MCP_REG_MCPR_ACCESS_LOCK				 0x8009c
#define MCP_REG_MCPR_CPU_PROGRAM_COUNTER			 0x8501c
#define MCP_REG_MCPR_GP_INPUTS					 0x800c0
#define MCP_REG_MCPR_GP_OENABLE					 0x800c8
#define MCP_REG_MCPR_GP_OUTPUTS					 0x800c4
#define MCP_REG_MCPR_IMC_COMMAND				 0x85900
#define MCP_REG_MCPR_IMC_DATAREG0				 0x85920
#define MCP_REG_MCPR_IMC_SLAVE_CONTROL				 0x85904
#define MCP_REG_MCPR_CPU_PROGRAM_COUNTER			 0x8501c
#define MCP_REG_MCPR_NVM_ACCESS_ENABLE				 0x86424
#define MCP_REG_MCPR_NVM_ADDR					 0x8640c
#define MCP_REG_MCPR_NVM_CFG4					 0x8642c
#define MCP_REG_MCPR_NVM_COMMAND				 0x86400
#define MCP_REG_MCPR_NVM_READ					 0x86410
#define MCP_REG_MCPR_NVM_SW_ARB 				 0x86420
#define MCP_REG_MCPR_NVM_WRITE					 0x86408
#define MCP_REG_MCPR_SCRATCH					 0xa0000
#define MISC_AEU_GENERAL_MASK_REG_AEU_NIG_CLOSE_MASK		 (0x1<<1)
#define MISC_AEU_GENERAL_MASK_REG_AEU_PXP_CLOSE_MASK		 (0x1<<0)
/* [R 32] read first 32 bit after inversion of function 0. mapped as
   follows: [0] NIG attention for function0; [1] NIG attention for
   function1; [2] GPIO1 mcp; [3] GPIO2 mcp; [4] GPIO3 mcp; [5] GPIO4 mcp;
   [6] GPIO1 function 1; [7] GPIO2 function 1; [8] GPIO3 function 1; [9]
   GPIO4 function 1; [10] PCIE glue/PXP VPD event function0; [11] PCIE
   glue/PXP VPD event function1; [12] PCIE glue/PXP Expansion ROM event0;
   [13] PCIE glue/PXP Expansion ROM event1; [14] SPIO4; [15] SPIO5; [16]
   MSI/X indication for mcp; [17] MSI/X indication for function 1; [18] BRB
   Parity error; [19] BRB Hw interrupt; [20] PRS Parity error; [21] PRS Hw
   interrupt; [22] SRC Parity error; [23] SRC Hw interrupt; [24] TSDM Parity
   error; [25] TSDM Hw interrupt; [26] TCM Parity error; [27] TCM Hw
   interrupt; [28] TSEMI Parity error; [29] TSEMI Hw interrupt; [30] PBF
   Parity error; [31] PBF Hw interrupt; */
#define MISC_REG_AEU_AFTER_INVERT_1_FUNC_0			 0xa42c
#define MISC_REG_AEU_AFTER_INVERT_1_FUNC_1			 0xa430
/* [R 32] read first 32 bit after inversion of mcp. mapped as follows: [0]
   NIG attention for function0; [1] NIG attention for function1; [2] GPIO1
   mcp; [3] GPIO2 mcp; [4] GPIO3 mcp; [5] GPIO4 mcp; [6] GPIO1 function 1;
   [7] GPIO2 function 1; [8] GPIO3 function 1; [9] GPIO4 function 1; [10]
   PCIE glue/PXP VPD event function0; [11] PCIE glue/PXP VPD event
   function1; [12] PCIE glue/PXP Expansion ROM event0; [13] PCIE glue/PXP
   Expansion ROM event1; [14] SPIO4; [15] SPIO5; [16] MSI/X indication for
   mcp; [17] MSI/X indication for function 1; [18] BRB Parity error; [19]
   BRB Hw interrupt; [20] PRS Parity error; [21] PRS Hw interrupt; [22] SRC
   Parity error; [23] SRC Hw interrupt; [24] TSDM Parity error; [25] TSDM Hw
   interrupt; [26] TCM Parity error; [27] TCM Hw interrupt; [28] TSEMI
   Parity error; [29] TSEMI Hw interrupt; [30] PBF Parity error; [31] PBF Hw
   interrupt; */
#define MISC_REG_AEU_AFTER_INVERT_1_MCP 			 0xa434
/* [R 32] read second 32 bit after inversion of function 0. mapped as
   follows: [0] PBClient Parity error; [1] PBClient Hw interrupt; [2] QM
   Parity error; [3] QM Hw interrupt; [4] Timers Parity error; [5] Timers Hw
   interrupt; [6] XSDM Parity error; [7] XSDM Hw interrupt; [8] XCM Parity
   error; [9] XCM Hw interrupt; [10] XSEMI Parity error; [11] XSEMI Hw
   interrupt; [12] DoorbellQ Parity error; [13] DoorbellQ Hw interrupt; [14]
   NIG Parity error; [15] NIG Hw interrupt; [16] Vaux PCI core Parity error;
   [17] Vaux PCI core Hw interrupt; [18] Debug Parity error; [19] Debug Hw
   interrupt; [20] USDM Parity error; [21] USDM Hw interrupt; [22] UCM
   Parity error; [23] UCM Hw interrupt; [24] USEMI Parity error; [25] USEMI
   Hw interrupt; [26] UPB Parity error; [27] UPB Hw interrupt; [28] CSDM
   Parity error; [29] CSDM Hw interrupt; [30] CCM Parity error; [31] CCM Hw
   interrupt; */
#define MISC_REG_AEU_AFTER_INVERT_2_FUNC_0			 0xa438
#define MISC_REG_AEU_AFTER_INVERT_2_FUNC_1			 0xa43c
/* [R 32] read second 32 bit after inversion of mcp. mapped as follows: [0]
   PBClient Parity error; [1] PBClient Hw interrupt; [2] QM Parity error;
   [3] QM Hw interrupt; [4] Timers Parity error; [5] Timers Hw interrupt;
   [6] XSDM Parity error; [7] XSDM Hw interrupt; [8] XCM Parity error; [9]
   XCM Hw interrupt; [10] XSEMI Parity error; [11] XSEMI Hw interrupt; [12]
   DoorbellQ Parity error; [13] DoorbellQ Hw interrupt; [14] NIG Parity
   error; [15] NIG Hw interrupt; [16] Vaux PCI core Parity error; [17] Vaux
   PCI core Hw interrupt; [18] Debug Parity error; [19] Debug Hw interrupt;
   [20] USDM Parity error; [21] USDM Hw interrupt; [22] UCM Parity error;
   [23] UCM Hw interrupt; [24] USEMI Parity error; [25] USEMI Hw interrupt;
   [26] UPB Parity error; [27] UPB Hw interrupt; [28] CSDM Parity error;
   [29] CSDM Hw interrupt; [30] CCM Parity error; [31] CCM Hw interrupt; */
#define MISC_REG_AEU_AFTER_INVERT_2_MCP 			 0xa440
/* [R 32] read third 32 bit after inversion of function 0. mapped as
   follows: [0] CSEMI Parity error; [1] CSEMI Hw interrupt; [2] PXP Parity
   error; [3] PXP Hw interrupt; [4] PXPpciClockClient Parity error; [5]
   PXPpciClockClient Hw interrupt; [6] CFC Parity error; [7] CFC Hw
   interrupt; [8] CDU Parity error; [9] CDU Hw interrupt; [10] DMAE Parity
   error; [11] DMAE Hw interrupt; [12] IGU (HC) Parity error; [13] IGU (HC)
   Hw interrupt; [14] MISC Parity error; [15] MISC Hw interrupt; [16]
   pxp_misc_mps_attn; [17] Flash event; [18] SMB event; [19] MCP attn0; [20]
   MCP attn1; [21] SW timers attn_1 func0; [22] SW timers attn_2 func0; [23]
   SW timers attn_3 func0; [24] SW timers attn_4 func0; [25] PERST; [26] SW
   timers attn_1 func1; [27] SW timers attn_2 func1; [28] SW timers attn_3
   func1; [29] SW timers attn_4 func1; [30] General attn0; [31] General
   attn1; */
#define MISC_REG_AEU_AFTER_INVERT_3_FUNC_0			 0xa444
#define MISC_REG_AEU_AFTER_INVERT_3_FUNC_1			 0xa448
/* [R 32] read third 32 bit after inversion of mcp. mapped as follows: [0]
   CSEMI Parity error; [1] CSEMI Hw interrupt; [2] PXP Parity error; [3] PXP
   Hw interrupt; [4] PXPpciClockClient Parity error; [5] PXPpciClockClient
   Hw interrupt; [6] CFC Parity error; [7] CFC Hw interrupt; [8] CDU Parity
   error; [9] CDU Hw interrupt; [10] DMAE Parity error; [11] DMAE Hw
   interrupt; [12] IGU (HC) Parity error; [13] IGU (HC) Hw interrupt; [14]
   MISC Parity error; [15] MISC Hw interrupt; [16] pxp_misc_mps_attn; [17]
   Flash event; [18] SMB event; [19] MCP attn0; [20] MCP attn1; [21] SW
   timers attn_1 func0; [22] SW timers attn_2 func0; [23] SW timers attn_3
   func0; [24] SW timers attn_4 func0; [25] PERST; [26] SW timers attn_1
   func1; [27] SW timers attn_2 func1; [28] SW timers attn_3 func1; [29] SW
   timers attn_4 func1; [30] General attn0; [31] General attn1; */
#define MISC_REG_AEU_AFTER_INVERT_3_MCP 			 0xa44c
/* [R 32] read fourth 32 bit after inversion of function 0. mapped as
   follows: [0] General attn2; [1] General attn3; [2] General attn4; [3]
   General attn5; [4] General attn6; [5] General attn7; [6] General attn8;
   [7] General attn9; [8] General attn10; [9] General attn11; [10] General
   attn12; [11] General attn13; [12] General attn14; [13] General attn15;
   [14] General attn16; [15] General attn17; [16] General attn18; [17]
   General attn19; [18] General attn20; [19] General attn21; [20] Main power
   interrupt; [21] RBCR Latched attn; [22] RBCT Latched attn; [23] RBCN
   Latched attn; [24] RBCU Latched attn; [25] RBCP Latched attn; [26] GRC
   Latched timeout attention; [27] GRC Latched reserved access attention;
   [28] MCP Latched rom_parity; [29] MCP Latched ump_rx_parity; [30] MCP
   Latched ump_tx_parity; [31] MCP Latched scpad_parity; */
#define MISC_REG_AEU_AFTER_INVERT_4_FUNC_0			 0xa450
#define MISC_REG_AEU_AFTER_INVERT_4_FUNC_1			 0xa454
/* [R 32] read fourth 32 bit after inversion of mcp. mapped as follows: [0]
   General attn2; [1] General attn3; [2] General attn4; [3] General attn5;
   [4] General attn6; [5] General attn7; [6] General attn8; [7] General
   attn9; [8] General attn10; [9] General attn11; [10] General attn12; [11]
   General attn13; [12] General attn14; [13] General attn15; [14] General
   attn16; [15] General attn17; [16] General attn18; [17] General attn19;
   [18] General attn20; [19] General attn21; [20] Main power interrupt; [21]
   RBCR Latched attn; [22] RBCT Latched attn; [23] RBCN Latched attn; [24]
   RBCU Latched attn; [25] RBCP Latched attn; [26] GRC Latched timeout
   attention; [27] GRC Latched reserved access attention; [28] MCP Latched
   rom_parity; [29] MCP Latched ump_rx_parity; [30] MCP Latched
   ump_tx_parity; [31] MCP Latched scpad_parity; */
#define MISC_REG_AEU_AFTER_INVERT_4_MCP 			 0xa458
/* [R 32] Read fifth 32 bit after inversion of function 0. Mapped as
 * follows: [0] PGLUE config_space; [1] PGLUE misc_flr; [2] PGLUE B RBC
 * attention [3] PGLUE B RBC parity; [4] ATC attention; [5] ATC parity; [6]
 * CNIG attention (reserved); [7] CNIG parity (reserved); [31-8] Reserved; */
#define MISC_REG_AEU_AFTER_INVERT_5_FUNC_0			 0xa700
/* [W 14] write to this register results with the clear of the latched
   signals; one in d0 clears RBCR latch; one in d1 clears RBCT latch; one in
   d2 clears RBCN latch; one in d3 clears RBCU latch; one in d4 clears RBCP
   latch; one in d5 clears GRC Latched timeout attention; one in d6 clears
   GRC Latched reserved access attention; one in d7 clears Latched
   rom_parity; one in d8 clears Latched ump_rx_parity; one in d9 clears
   Latched ump_tx_parity; one in d10 clears Latched scpad_parity (both
   ports); one in d11 clears pxpv_misc_mps_attn; one in d12 clears
   pxp_misc_exp_rom_attn0; one in d13 clears pxp_misc_exp_rom_attn1; read
   from this register return zero */
#define MISC_REG_AEU_CLR_LATCH_SIGNAL				 0xa45c
/* [RW 32] first 32b for enabling the output for function 0 output0. mapped
   as follows: [0] NIG attention for function0; [1] NIG attention for
   function1; [2] GPIO1 function 0; [3] GPIO2 function 0; [4] GPIO3 function
   0; [5] GPIO4 function 0; [6] GPIO1 function 1; [7] GPIO2 function 1; [8]
   GPIO3 function 1; [9] GPIO4 function 1; [10] PCIE glue/PXP VPD event
   function0; [11] PCIE glue/PXP VPD event function1; [12] PCIE glue/PXP
   Expansion ROM event0; [13] PCIE glue/PXP Expansion ROM event1; [14]
   SPIO4; [15] SPIO5; [16] MSI/X indication for function 0; [17] MSI/X
   indication for function 1; [18] BRB Parity error; [19] BRB Hw interrupt;
   [20] PRS Parity error; [21] PRS Hw interrupt; [22] SRC Parity error; [23]
   SRC Hw interrupt; [24] TSDM Parity error; [25] TSDM Hw interrupt; [26]
   TCM Parity error; [27] TCM Hw interrupt; [28] TSEMI Parity error; [29]
   TSEMI Hw interrupt; [30] PBF Parity error; [31] PBF Hw interrupt; */
#define MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0			 0xa06c
#define MISC_REG_AEU_ENABLE1_FUNC_0_OUT_1			 0xa07c
#define MISC_REG_AEU_ENABLE1_FUNC_0_OUT_2			 0xa08c
#define MISC_REG_AEU_ENABLE1_FUNC_0_OUT_3			 0xa09c
#define MISC_REG_AEU_ENABLE1_FUNC_0_OUT_5			 0xa0bc
#define MISC_REG_AEU_ENABLE1_FUNC_0_OUT_6			 0xa0cc
#define MISC_REG_AEU_ENABLE1_FUNC_0_OUT_7			 0xa0dc
/* [RW 32] first 32b for enabling the output for function 1 output0. mapped
   as follows: [0] NIG attention for function0; [1] NIG attention for
   function1; [2] GPIO1 function 1; [3] GPIO2 function 1; [4] GPIO3 function
   1; [5] GPIO4 function 1; [6] GPIO1 function 1; [7] GPIO2 function 1; [8]
   GPIO3 function 1; [9] GPIO4 function 1; [10] PCIE glue/PXP VPD event
   function0; [11] PCIE glue/PXP VPD event function1; [12] PCIE glue/PXP
   Expansion ROM event0; [13] PCIE glue/PXP Expansion ROM event1; [14]
   SPIO4; [15] SPIO5; [16] MSI/X indication for function 1; [17] MSI/X
   indication for function 1; [18] BRB Parity error; [19] BRB Hw interrupt;
   [20] PRS Parity error; [21] PRS Hw interrupt; [22] SRC Parity error; [23]
   SRC Hw interrupt; [24] TSDM Parity error; [25] TSDM Hw interrupt; [26]
   TCM Parity error; [27] TCM Hw interrupt; [28] TSEMI Parity error; [29]
   TSEMI Hw interrupt; [30] PBF Parity error; [31] PBF Hw interrupt; */
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0			 0xa10c
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_1			 0xa11c
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_2			 0xa12c
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_3			 0xa13c
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_5			 0xa15c
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_6			 0xa16c
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_7			 0xa17c
/* [RW 32] first 32b for enabling the output for close the gate nig. mapped
   as follows: [0] NIG attention for function0; [1] NIG attention for
   function1; [2] GPIO1 function 0; [3] GPIO2 function 0; [4] GPIO3 function
   0; [5] GPIO4 function 0; [6] GPIO1 function 1; [7] GPIO2 function 1; [8]
   GPIO3 function 1; [9] GPIO4 function 1; [10] PCIE glue/PXP VPD event
   function0; [11] PCIE glue/PXP VPD event function1; [12] PCIE glue/PXP
   Expansion ROM event0; [13] PCIE glue/PXP Expansion ROM event1; [14]
   SPIO4; [15] SPIO5; [16] MSI/X indication for function 0; [17] MSI/X
   indication for function 1; [18] BRB Parity error; [19] BRB Hw interrupt;
   [20] PRS Parity error; [21] PRS Hw interrupt; [22] SRC Parity error; [23]
   SRC Hw interrupt; [24] TSDM Parity error; [25] TSDM Hw interrupt; [26]
   TCM Parity error; [27] TCM Hw interrupt; [28] TSEMI Parity error; [29]
   TSEMI Hw interrupt; [30] PBF Parity error; [31] PBF Hw interrupt; */
#define MISC_REG_AEU_ENABLE1_NIG_0				 0xa0ec
#define MISC_REG_AEU_ENABLE1_NIG_1				 0xa18c
/* [RW 32] first 32b for enabling the output for close the gate pxp. mapped
   as follows: [0] NIG attention for function0; [1] NIG attention for
   function1; [2] GPIO1 function 0; [3] GPIO2 function 0; [4] GPIO3 function
   0; [5] GPIO4 function 0; [6] GPIO1 function 1; [7] GPIO2 function 1; [8]
   GPIO3 function 1; [9] GPIO4 function 1; [10] PCIE glue/PXP VPD event
   function0; [11] PCIE glue/PXP VPD event function1; [12] PCIE glue/PXP
   Expansion ROM event0; [13] PCIE glue/PXP Expansion ROM event1; [14]
   SPIO4; [15] SPIO5; [16] MSI/X indication for function 0; [17] MSI/X
   indication for function 1; [18] BRB Parity error; [19] BRB Hw interrupt;
   [20] PRS Parity error; [21] PRS Hw interrupt; [22] SRC Parity error; [23]
   SRC Hw interrupt; [24] TSDM Parity error; [25] TSDM Hw interrupt; [26]
   TCM Parity error; [27] TCM Hw interrupt; [28] TSEMI Parity error; [29]
   TSEMI Hw interrupt; [30] PBF Parity error; [31] PBF Hw interrupt; */
#define MISC_REG_AEU_ENABLE1_PXP_0				 0xa0fc
#define MISC_REG_AEU_ENABLE1_PXP_1				 0xa19c
/* [RW 32] second 32b for enabling the output for function 0 output0. mapped
   as follows: [0] PBClient Parity error; [1] PBClient Hw interrupt; [2] QM
   Parity error; [3] QM Hw interrupt; [4] Timers Parity error; [5] Timers Hw
   interrupt; [6] XSDM Parity error; [7] XSDM Hw interrupt; [8] XCM Parity
   error; [9] XCM Hw interrupt; [10] XSEMI Parity error; [11] XSEMI Hw
   interrupt; [12] DoorbellQ Parity error; [13] DoorbellQ Hw interrupt; [14]
   NIG Parity error; [15] NIG Hw interrupt; [16] Vaux PCI core Parity error;
   [17] Vaux PCI core Hw interrupt; [18] Debug Parity error; [19] Debug Hw
   interrupt; [20] USDM Parity error; [21] USDM Hw interrupt; [22] UCM
   Parity error; [23] UCM Hw interrupt; [24] USEMI Parity error; [25] USEMI
   Hw interrupt; [26] UPB Parity error; [27] UPB Hw interrupt; [28] CSDM
   Parity error; [29] CSDM Hw interrupt; [30] CCM Parity error; [31] CCM Hw
   interrupt; */
#define MISC_REG_AEU_ENABLE2_FUNC_0_OUT_0			 0xa070
#define MISC_REG_AEU_ENABLE2_FUNC_0_OUT_1			 0xa080
/* [RW 32] second 32b for enabling the output for function 1 output0. mapped
   as follows: [0] PBClient Parity error; [1] PBClient Hw interrupt; [2] QM
   Parity error; [3] QM Hw interrupt; [4] Timers Parity error; [5] Timers Hw
   interrupt; [6] XSDM Parity error; [7] XSDM Hw interrupt; [8] XCM Parity
   error; [9] XCM Hw interrupt; [10] XSEMI Parity error; [11] XSEMI Hw
   interrupt; [12] DoorbellQ Parity error; [13] DoorbellQ Hw interrupt; [14]
   NIG Parity error; [15] NIG Hw interrupt; [16] Vaux PCI core Parity error;
   [17] Vaux PCI core Hw interrupt; [18] Debug Parity error; [19] Debug Hw
   interrupt; [20] USDM Parity error; [21] USDM Hw interrupt; [22] UCM
   Parity error; [23] UCM Hw interrupt; [24] USEMI Parity error; [25] USEMI
   Hw interrupt; [26] UPB Parity error; [27] UPB Hw interrupt; [28] CSDM
   Parity error; [29] CSDM Hw interrupt; [30] CCM Parity error; [31] CCM Hw
   interrupt; */
#define MISC_REG_AEU_ENABLE2_FUNC_1_OUT_0			 0xa110
#define MISC_REG_AEU_ENABLE2_FUNC_1_OUT_1			 0xa120
/* [RW 32] second 32b for enabling the output for close the gate nig. mapped
   as follows: [0] PBClient Parity error; [1] PBClient Hw interrupt; [2] QM
   Parity error; [3] QM Hw interrupt; [4] Timers Parity error; [5] Timers Hw
   interrupt; [6] XSDM Parity error; [7] XSDM Hw interrupt; [8] XCM Parity
   error; [9] XCM Hw interrupt; [10] XSEMI Parity error; [11] XSEMI Hw
   interrupt; [12] DoorbellQ Parity error; [13] DoorbellQ Hw interrupt; [14]
   NIG Parity error; [15] NIG Hw interrupt; [16] Vaux PCI core Parity error;
   [17] Vaux PCI core Hw interrupt; [18] Debug Parity error; [19] Debug Hw
   interrupt; [20] USDM Parity error; [21] USDM Hw interrupt; [22] UCM
   Parity error; [23] UCM Hw interrupt; [24] USEMI Parity error; [25] USEMI
   Hw interrupt; [26] UPB Parity error; [27] UPB Hw interrupt; [28] CSDM
   Parity error; [29] CSDM Hw interrupt; [30] CCM Parity error; [31] CCM Hw
   interrupt; */
#define MISC_REG_AEU_ENABLE2_NIG_0				 0xa0f0
#define MISC_REG_AEU_ENABLE2_NIG_1				 0xa190
/* [RW 32] second 32b for enabling the output for close the gate pxp. mapped
   as follows: [0] PBClient Parity error; [1] PBClient Hw interrupt; [2] QM
   Parity error; [3] QM Hw interrupt; [4] Timers Parity error; [5] Timers Hw
   interrupt; [6] XSDM Parity error; [7] XSDM Hw interrupt; [8] XCM Parity
   error; [9] XCM Hw interrupt; [10] XSEMI Parity error; [11] XSEMI Hw
   interrupt; [12] DoorbellQ Parity error; [13] DoorbellQ Hw interrupt; [14]
   NIG Parity error; [15] NIG Hw interrupt; [16] Vaux PCI core Parity error;
   [17] Vaux PCI core Hw interrupt; [18] Debug Parity error; [19] Debug Hw
   interrupt; [20] USDM Parity error; [21] USDM Hw interrupt; [22] UCM
   Parity error; [23] UCM Hw interrupt; [24] USEMI Parity error; [25] USEMI
   Hw interrupt; [26] UPB Parity error; [27] UPB Hw interrupt; [28] CSDM
   Parity error; [29] CSDM Hw interrupt; [30] CCM Parity error; [31] CCM Hw
   interrupt; */
#define MISC_REG_AEU_ENABLE2_PXP_0				 0xa100
#define MISC_REG_AEU_ENABLE2_PXP_1				 0xa1a0
/* [RW 32] third 32b for enabling the output for function 0 output0. mapped
   as follows: [0] CSEMI Parity error; [1] CSEMI Hw interrupt; [2] PXP
   Parity error; [3] PXP Hw interrupt; [4] PXPpciClockClient Parity error;
   [5] PXPpciClockClient Hw interrupt; [6] CFC Parity error; [7] CFC Hw
   interrupt; [8] CDU Parity error; [9] CDU Hw interrupt; [10] DMAE Parity
   error; [11] DMAE Hw interrupt; [12] IGU (HC) Parity error; [13] IGU (HC)
   Hw interrupt; [14] MISC Parity error; [15] MISC Hw interrupt; [16]
   pxp_misc_mps_attn; [17] Flash event; [18] SMB event; [19] MCP attn0; [20]
   MCP attn1; [21] SW timers attn_1 func0; [22] SW timers attn_2 func0; [23]
   SW timers attn_3 func0; [24] SW timers attn_4 func0; [25] PERST; [26] SW
   timers attn_1 func1; [27] SW timers attn_2 func1; [28] SW timers attn_3
   func1; [29] SW timers attn_4 func1; [30] General attn0; [31] General
   attn1; */
#define MISC_REG_AEU_ENABLE3_FUNC_0_OUT_0			 0xa074
#define MISC_REG_AEU_ENABLE3_FUNC_0_OUT_1			 0xa084
/* [RW 32] third 32b for enabling the output for function 1 output0. mapped
   as follows: [0] CSEMI Parity error; [1] CSEMI Hw interrupt; [2] PXP
   Parity error; [3] PXP Hw interrupt; [4] PXPpciClockClient Parity error;
   [5] PXPpciClockClient Hw interrupt; [6] CFC Parity error; [7] CFC Hw
   interrupt; [8] CDU Parity error; [9] CDU Hw interrupt; [10] DMAE Parity
   error; [11] DMAE Hw interrupt; [12] IGU (HC) Parity error; [13] IGU (HC)
   Hw interrupt; [14] MISC Parity error; [15] MISC Hw interrupt; [16]
   pxp_misc_mps_attn; [17] Flash event; [18] SMB event; [19] MCP attn0; [20]
   MCP attn1; [21] SW timers attn_1 func0; [22] SW timers attn_2 func0; [23]
   SW timers attn_3 func0; [24] SW timers attn_4 func0; [25] PERST; [26] SW
   timers attn_1 func1; [27] SW timers attn_2 func1; [28] SW timers attn_3
   func1; [29] SW timers attn_4 func1; [30] General attn0; [31] General
   attn1; */
#define MISC_REG_AEU_ENABLE3_FUNC_1_OUT_0			 0xa114
#define MISC_REG_AEU_ENABLE3_FUNC_1_OUT_1			 0xa124
/* [RW 32] third 32b for enabling the output for close the gate nig. mapped
   as follows: [0] CSEMI Parity error; [1] CSEMI Hw interrupt; [2] PXP
   Parity error; [3] PXP Hw interrupt; [4] PXPpciClockClient Parity error;
   [5] PXPpciClockClient Hw interrupt; [6] CFC Parity error; [7] CFC Hw
   interrupt; [8] CDU Parity error; [9] CDU Hw interrupt; [10] DMAE Parity
   error; [11] DMAE Hw interrupt; [12] IGU (HC) Parity error; [13] IGU (HC)
   Hw interrupt; [14] MISC Parity error; [15] MISC Hw interrupt; [16]
   pxp_misc_mps_attn; [17] Flash event; [18] SMB event; [19] MCP attn0; [20]
   MCP attn1; [21] SW timers attn_1 func0; [22] SW timers attn_2 func0; [23]
   SW timers attn_3 func0; [24] SW timers attn_4 func0; [25] PERST; [26] SW
   timers attn_1 func1; [27] SW timers attn_2 func1; [28] SW timers attn_3
   func1; [29] SW timers attn_4 func1; [30] General attn0; [31] General
   attn1; */
#define MISC_REG_AEU_ENABLE3_NIG_0				 0xa0f4
#define MISC_REG_AEU_ENABLE3_NIG_1				 0xa194
/* [RW 32] third 32b for enabling the output for close the gate pxp. mapped
   as follows: [0] CSEMI Parity error; [1] CSEMI Hw interrupt; [2] PXP
   Parity error; [3] PXP Hw interrupt; [4] PXPpciClockClient Parity error;
   [5] PXPpciClockClient Hw interrupt; [6] CFC Parity error; [7] CFC Hw
   interrupt; [8] CDU Parity error; [9] CDU Hw interrupt; [10] DMAE Parity
   error; [11] DMAE Hw interrupt; [12] IGU (HC) Parity error; [13] IGU (HC)
   Hw interrupt; [14] MISC Parity error; [15] MISC Hw interrupt; [16]
   pxp_misc_mps_attn; [17] Flash event; [18] SMB event; [19] MCP attn0; [20]
   MCP attn1; [21] SW timers attn_1 func0; [22] SW timers attn_2 func0; [23]
   SW timers attn_3 func0; [24] SW timers attn_4 func0; [25] PERST; [26] SW
   timers attn_1 func1; [27] SW timers attn_2 func1; [28] SW timers attn_3
   func1; [29] SW timers attn_4 func1; [30] General attn0; [31] General
   attn1; */
#define MISC_REG_AEU_ENABLE3_PXP_0				 0xa104
#define MISC_REG_AEU_ENABLE3_PXP_1				 0xa1a4
/* [RW 32] fourth 32b for enabling the output for function 0 output0.mapped
   as follows: [0] General attn2; [1] General attn3; [2] General attn4; [3]
   General attn5; [4] General attn6; [5] General attn7; [6] General attn8;
   [7] General attn9; [8] General attn10; [9] General attn11; [10] General
   attn12; [11] General attn13; [12] General attn14; [13] General attn15;
   [14] General attn16; [15] General attn17; [16] General attn18; [17]
   General attn19; [18] General attn20; [19] General attn21; [20] Main power
   interrupt; [21] RBCR Latched attn; [22] RBCT Latched attn; [23] RBCN
   Latched attn; [24] RBCU Latched attn; [25] RBCP Latched attn; [26] GRC
   Latched timeout attention; [27] GRC Latched reserved access attention;
   [28] MCP Latched rom_parity; [29] MCP Latched ump_rx_parity; [30] MCP
   Latched ump_tx_parity; [31] MCP Latched scpad_parity; */
#define MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0			 0xa078
#define MISC_REG_AEU_ENABLE4_FUNC_0_OUT_2			 0xa098
#define MISC_REG_AEU_ENABLE4_FUNC_0_OUT_4			 0xa0b8
#define MISC_REG_AEU_ENABLE4_FUNC_0_OUT_5			 0xa0c8
#define MISC_REG_AEU_ENABLE4_FUNC_0_OUT_6			 0xa0d8
#define MISC_REG_AEU_ENABLE4_FUNC_0_OUT_7			 0xa0e8
/* [RW 32] fourth 32b for enabling the output for function 1 output0.mapped
   as follows: [0] General attn2; [1] General attn3; [2] General attn4; [3]
   General attn5; [4] General attn6; [5] General attn7; [6] General attn8;
   [7] General attn9; [8] General attn10; [9] General attn11; [10] General
   attn12; [11] General attn13; [12] General attn14; [13] General attn15;
   [14] General attn16; [15] General attn17; [16] General attn18; [17]
   General attn19; [18] General attn20; [19] General attn21; [20] Main power
   interrupt; [21] RBCR Latched attn; [22] RBCT Latched attn; [23] RBCN
   Latched attn; [24] RBCU Latched attn; [25] RBCP Latched attn; [26] GRC
   Latched timeout attention; [27] GRC Latched reserved access attention;
   [28] MCP Latched rom_parity; [29] MCP Latched ump_rx_parity; [30] MCP
   Latched ump_tx_parity; [31] MCP Latched scpad_parity; */
#define MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0			 0xa118
#define MISC_REG_AEU_ENABLE4_FUNC_1_OUT_2			 0xa138
#define MISC_REG_AEU_ENABLE4_FUNC_1_OUT_4			 0xa158
#define MISC_REG_AEU_ENABLE4_FUNC_1_OUT_5			 0xa168
#define MISC_REG_AEU_ENABLE4_FUNC_1_OUT_6			 0xa178
#define MISC_REG_AEU_ENABLE4_FUNC_1_OUT_7			 0xa188
/* [RW 32] fourth 32b for enabling the output for close the gate nig.mapped
   as follows: [0] General attn2; [1] General attn3; [2] General attn4; [3]
   General attn5; [4] General attn6; [5] General attn7; [6] General attn8;
   [7] General attn9; [8] General attn10; [9] General attn11; [10] General
   attn12; [11] General attn13; [12] General attn14; [13] General attn15;
   [14] General attn16; [15] General attn17; [16] General attn18; [17]
   General attn19; [18] General attn20; [19] General attn21; [20] Main power
   interrupt; [21] RBCR Latched attn; [22] RBCT Latched attn; [23] RBCN
   Latched attn; [24] RBCU Latched attn; [25] RBCP Latched attn; [26] GRC
   Latched timeout attention; [27] GRC Latched reserved access attention;
   [28] MCP Latched rom_parity; [29] MCP Latched ump_rx_parity; [30] MCP
   Latched ump_tx_parity; [31] MCP Latched scpad_parity; */
#define MISC_REG_AEU_ENABLE4_NIG_0				 0xa0f8
#define MISC_REG_AEU_ENABLE4_NIG_1				 0xa198
/* [RW 32] fourth 32b for enabling the output for close the gate pxp.mapped
   as follows: [0] General attn2; [1] General attn3; [2] General attn4; [3]
   General attn5; [4] General attn6; [5] General attn7; [6] General attn8;
   [7] General attn9; [8] General attn10; [9] General attn11; [10] General
   attn12; [11] General attn13; [12] General attn14; [13] General attn15;
   [14] General attn16; [15] General attn17; [16] General attn18; [17]
   General attn19; [18] General attn20; [19] General attn21; [20] Main power
   interrupt; [21] RBCR Latched attn; [22] RBCT Latched attn; [23] RBCN
   Latched attn; [24] RBCU Latched attn; [25] RBCP Latched attn; [26] GRC
   Latched timeout attention; [27] GRC Latched reserved access attention;
   [28] MCP Latched rom_parity; [29] MCP Latched ump_rx_parity; [30] MCP
   Latched ump_tx_parity; [31] MCP Latched scpad_parity; */
#define MISC_REG_AEU_ENABLE4_PXP_0				 0xa108
#define MISC_REG_AEU_ENABLE4_PXP_1				 0xa1a8
/* [RW 32] fifth 32b for enabling the output for function 0 output0. Mapped
 * as follows: [0] PGLUE config_space; [1] PGLUE misc_flr; [2] PGLUE B RBC
 * attention [3] PGLUE B RBC parity; [4] ATC attention; [5] ATC parity; [6]
 * mstat0 attention; [7] mstat0 parity; [8] mstat1 attention; [9] mstat1
 * parity; [31-10] Reserved; */
#define MISC_REG_AEU_ENABLE5_FUNC_0_OUT_0			 0xa688
/* [RW 32] Fifth 32b for enabling the output for function 1 output0. Mapped
 * as follows: [0] PGLUE config_space; [1] PGLUE misc_flr; [2] PGLUE B RBC
 * attention [3] PGLUE B RBC parity; [4] ATC attention; [5] ATC parity; [6]
 * mstat0 attention; [7] mstat0 parity; [8] mstat1 attention; [9] mstat1
 * parity; [31-10] Reserved; */
#define MISC_REG_AEU_ENABLE5_FUNC_1_OUT_0			 0xa6b0
/* [RW 1] set/clr general attention 0; this will set/clr bit 94 in the aeu
   128 bit vector */
#define MISC_REG_AEU_GENERAL_ATTN_0				 0xa000
#define MISC_REG_AEU_GENERAL_ATTN_1				 0xa004
#define MISC_REG_AEU_GENERAL_ATTN_10				 0xa028
#define MISC_REG_AEU_GENERAL_ATTN_11				 0xa02c
#define MISC_REG_AEU_GENERAL_ATTN_12				 0xa030
#define MISC_REG_AEU_GENERAL_ATTN_2				 0xa008
#define MISC_REG_AEU_GENERAL_ATTN_3				 0xa00c
#define MISC_REG_AEU_GENERAL_ATTN_4				 0xa010
#define MISC_REG_AEU_GENERAL_ATTN_5				 0xa014
#define MISC_REG_AEU_GENERAL_ATTN_6				 0xa018
#define MISC_REG_AEU_GENERAL_ATTN_7				 0xa01c
#define MISC_REG_AEU_GENERAL_ATTN_8				 0xa020
#define MISC_REG_AEU_GENERAL_ATTN_9				 0xa024
#define MISC_REG_AEU_GENERAL_MASK				 0xa61c
/* [RW 32] first 32b for inverting the input for function 0; for each bit:
   0= do not invert; 1= invert; mapped as follows: [0] NIG attention for
   function0; [1] NIG attention for function1; [2] GPIO1 mcp; [3] GPIO2 mcp;
   [4] GPIO3 mcp; [5] GPIO4 mcp; [6] GPIO1 function 1; [7] GPIO2 function 1;
   [8] GPIO3 function 1; [9] GPIO4 function 1; [10] PCIE glue/PXP VPD event
   function0; [11] PCIE glue/PXP VPD event function1; [12] PCIE glue/PXP
   Expansion ROM event0; [13] PCIE glue/PXP Expansion ROM event1; [14]
   SPIO4; [15] SPIO5; [16] MSI/X indication for mcp; [17] MSI/X indication
   for function 1; [18] BRB Parity error; [19] BRB Hw interrupt; [20] PRS
   Parity error; [21] PRS Hw interrupt; [22] SRC Parity error; [23] SRC Hw
   interrupt; [24] TSDM Parity error; [25] TSDM Hw interrupt; [26] TCM
   Parity error; [27] TCM Hw interrupt; [28] TSEMI Parity error; [29] TSEMI
   Hw interrupt; [30] PBF Parity error; [31] PBF Hw interrupt; */
#define MISC_REG_AEU_INVERTER_1_FUNC_0				 0xa22c
#define MISC_REG_AEU_INVERTER_1_FUNC_1				 0xa23c
/* [RW 32] second 32b for inverting the input for function 0; for each bit:
   0= do not invert; 1= invert. mapped as follows: [0] PBClient Parity
   error; [1] PBClient Hw interrupt; [2] QM Parity error; [3] QM Hw
   interrupt; [4] Timers Parity error; [5] Timers Hw interrupt; [6] XSDM
   Parity error; [7] XSDM Hw interrupt; [8] XCM Parity error; [9] XCM Hw
   interrupt; [10] XSEMI Parity error; [11] XSEMI Hw interrupt; [12]
   DoorbellQ Parity error; [13] DoorbellQ Hw interrupt; [14] NIG Parity
   error; [15] NIG Hw interrupt; [16] Vaux PCI core Parity error; [17] Vaux
   PCI core Hw interrupt; [18] Debug Parity error; [19] Debug Hw interrupt;
   [20] USDM Parity error; [21] USDM Hw interrupt; [22] UCM Parity error;
   [23] UCM Hw interrupt; [24] USEMI Parity error; [25] USEMI Hw interrupt;
   [26] UPB Parity error; [27] UPB Hw interrupt; [28] CSDM Parity error;
   [29] CSDM Hw interrupt; [30] CCM Parity error; [31] CCM Hw interrupt; */
#define MISC_REG_AEU_INVERTER_2_FUNC_0				 0xa230
#define MISC_REG_AEU_INVERTER_2_FUNC_1				 0xa240
/* [RW 10] [7:0] = mask 8 attention output signals toward IGU function0;
   [9:8] = raserved. Zero = mask; one = unmask */
#define MISC_REG_AEU_MASK_ATTN_FUNC_0				 0xa060
#define MISC_REG_AEU_MASK_ATTN_FUNC_1				 0xa064
/* [RW 1] If set a system kill occurred */
#define MISC_REG_AEU_SYS_KILL_OCCURRED				 0xa610
/* [RW 32] Represent the status of the input vector to the AEU when a system
   kill occurred. The register is reset in por reset. Mapped as follows: [0]
   NIG attention for function0; [1] NIG attention for function1; [2] GPIO1
   mcp; [3] GPIO2 mcp; [4] GPIO3 mcp; [5] GPIO4 mcp; [6] GPIO1 function 1;
   [7] GPIO2 function 1; [8] GPIO3 function 1; [9] GPIO4 function 1; [10]
   PCIE glue/PXP VPD event function0; [11] PCIE glue/PXP VPD event
   function1; [12] PCIE glue/PXP Expansion ROM event0; [13] PCIE glue/PXP
   Expansion ROM event1; [14] SPIO4; [15] SPIO5; [16] MSI/X indication for
   mcp; [17] MSI/X indication for function 1; [18] BRB Parity error; [19]
   BRB Hw interrupt; [20] PRS Parity error; [21] PRS Hw interrupt; [22] SRC
   Parity error; [23] SRC Hw interrupt; [24] TSDM Parity error; [25] TSDM Hw
   interrupt; [26] TCM Parity error; [27] TCM Hw interrupt; [28] TSEMI
   Parity error; [29] TSEMI Hw interrupt; [30] PBF Parity error; [31] PBF Hw
   interrupt; */
#define MISC_REG_AEU_SYS_KILL_STATUS_0				 0xa600
#define MISC_REG_AEU_SYS_KILL_STATUS_1				 0xa604
#define MISC_REG_AEU_SYS_KILL_STATUS_2				 0xa608
#define MISC_REG_AEU_SYS_KILL_STATUS_3				 0xa60c
/* [R 4] This field indicates the type of the device. '0' - 2 Ports; '1' - 1
   Port. */
#define MISC_REG_BOND_ID					 0xa400
/* [R 8] These bits indicate the metal revision of the chip. This value
   starts at 0x00 for each all-layer tape-out and increments by one for each
   tape-out. */
#define MISC_REG_CHIP_METAL					 0xa404
/* [R 16] These bits indicate the part number for the chip. */
#define MISC_REG_CHIP_NUM					 0xa408
/* [R 4] These bits indicate the base revision of the chip. This value
   starts at 0x0 for the A0 tape-out and increments by one for each
   all-layer tape-out. */
#define MISC_REG_CHIP_REV					 0xa40c
/* [RW 32] The following driver registers(1...16) represent 16 drivers and
   32 clients. Each client can be controlled by one driver only. One in each
   bit represent that this driver control the appropriate client (Ex: bit 5
   is set means this driver control client number 5). addr1 = set; addr0 =
   clear; read from both addresses will give the same result = status. write
   to address 1 will set a request to control all the clients that their
   appropriate bit (in the write command) is set. if the client is free (the
   appropriate bit in all the other drivers is clear) one will be written to
   that driver register; if the client isn't free the bit will remain zero.
   if the appropriate bit is set (the driver request to gain control on a
   client it already controls the ~MISC_REGISTERS_INT_STS.GENERIC_SW
   interrupt will be asserted). write to address 0 will set a request to
   free all the clients that their appropriate bit (in the write command) is
   set. if the appropriate bit is clear (the driver request to free a client
   it doesn't controls the ~MISC_REGISTERS_INT_STS.GENERIC_SW interrupt will
   be asserted). */
#define MISC_REG_DRIVER_CONTROL_1				 0xa510
#define MISC_REG_DRIVER_CONTROL_7				 0xa3c8
/* [RW 1] e1hmf for WOL. If clr WOL signal o the PXP will be send on bit 0
   only. */
#define MISC_REG_E1HMF_MODE					 0xa5f8
/* [R 1] Status of four port mode path swap input pin. */
#define MISC_REG_FOUR_PORT_PATH_SWAP				 0xa75c
/* [RW 2] 4 port path swap overwrite.[0] - Overwrite control; if it is 0 -
   the path_swap output is equal to 4 port mode path swap input pin; if it
   is 1 - the path_swap output is equal to bit[1] of this register; [1] -
   Overwrite value. If bit[0] of this register is 1 this is the value that
   receives the path_swap output. Reset on Hard reset. */
#define MISC_REG_FOUR_PORT_PATH_SWAP_OVWR			 0xa738
/* [R 1] Status of 4 port mode port swap input pin. */
#define MISC_REG_FOUR_PORT_PORT_SWAP				 0xa754
/* [RW 2] 4 port port swap overwrite.[0] - Overwrite control; if it is 0 -
   the port_swap output is equal to 4 port mode port swap input pin; if it
   is 1 - the port_swap output is equal to bit[1] of this register; [1] -
   Overwrite value. If bit[0] of this register is 1 this is the value that
   receives the port_swap output. Reset on Hard reset. */
#define MISC_REG_FOUR_PORT_PORT_SWAP_OVWR			 0xa734
/* [RW 32] Debug only: spare RW register reset by core reset */
#define MISC_REG_GENERIC_CR_0					 0xa460
#define MISC_REG_GENERIC_CR_1					 0xa464
/* [RW 32] Debug only: spare RW register reset by por reset */
#define MISC_REG_GENERIC_POR_1					 0xa474
/* [RW 32] Bit[0]: EPIO MODE SEL: Setting this bit to 1 will allow SW/FW to
   use all of the 32 Extended GPIO pins. Without setting this bit; an EPIO
   can not be configured as an output. Each output has its output enable in
   the MCP register space; but this bit needs to be set to make use of that.
   Bit[3:1] spare. Bit[4]: WCVTMON_PWRDN: Powerdown for Warpcore VTMON. When
   set to 1 - Powerdown. Bit[5]: WCVTMON_RESETB: Reset for Warpcore VTMON.
   When set to 0 - vTMON is in reset. Bit[6]: setting this bit will change
   the i/o to an output and will drive the TimeSync output. Bit[31:7]:
   spare. Global register. Reset by hard reset. */
#define MISC_REG_GEN_PURP_HWG					 0xa9a0
/* [RW 32] GPIO. [31-28] FLOAT port 0; [27-24] FLOAT port 0; When any of
   these bits is written as a '1'; the corresponding SPIO bit will turn off
   it's drivers and become an input. This is the reset state of all GPIO
   pins. The read value of these bits will be a '1' if that last command
   (#SET; #CLR; or #FLOAT) for this bit was a #FLOAT. (reset value 0xff).
   [23-20] CLR port 1; 19-16] CLR port 0; When any of these bits is written
   as a '1'; the corresponding GPIO bit will drive low. The read value of
   these bits will be a '1' if that last command (#SET; #CLR; or #FLOAT) for
   this bit was a #CLR. (reset value 0). [15-12] SET port 1; 11-8] port 0;
   SET When any of these bits is written as a '1'; the corresponding GPIO
   bit will drive high (if it has that capability). The read value of these
   bits will be a '1' if that last command (#SET; #CLR; or #FLOAT) for this
   bit was a #SET. (reset value 0). [7-4] VALUE port 1; [3-0] VALUE port 0;
   RO; These bits indicate the read value of each of the eight GPIO pins.
   This is the result value of the pin; not the drive value. Writing these
   bits will have not effect. */
#define MISC_REG_GPIO						 0xa490
/* [RW 8] These bits enable the GPIO_INTs to signals event to the
   IGU/MCP.according to the following map: [0] p0_gpio_0; [1] p0_gpio_1; [2]
   p0_gpio_2; [3] p0_gpio_3; [4] p1_gpio_0; [5] p1_gpio_1; [6] p1_gpio_2;
   [7] p1_gpio_3; */
#define MISC_REG_GPIO_EVENT_EN					 0xa2bc
/* [RW 32] GPIO INT. [31-28] OLD_CLR port1; [27-24] OLD_CLR port0; Writing a
   '1' to these bit clears the corresponding bit in the #OLD_VALUE register.
   This will acknowledge an interrupt on the falling edge of corresponding
   GPIO input (reset value 0). [23-16] OLD_SET [23-16] port1; OLD_SET port0;
   Writing a '1' to these bit sets the corresponding bit in the #OLD_VALUE
   register. This will acknowledge an interrupt on the rising edge of
   corresponding SPIO input (reset value 0). [15-12] OLD_VALUE [11-8] port1;
   OLD_VALUE port0; RO; These bits indicate the old value of the GPIO input
   value. When the ~INT_STATE bit is set; this bit indicates the OLD value
   of the pin such that if ~INT_STATE is set and this bit is '0'; then the
   interrupt is due to a low to high edge. If ~INT_STATE is set and this bit
   is '1'; then the interrupt is due to a high to low edge (reset value 0).
   [7-4] INT_STATE port1; [3-0] INT_STATE RO port0; These bits indicate the
   current GPIO interrupt state for each GPIO pin. This bit is cleared when
   the appropriate #OLD_SET or #OLD_CLR command bit is written. This bit is
   set when the GPIO input does not match the current value in #OLD_VALUE
   (reset value 0). */
#define MISC_REG_GPIO_INT					 0xa494
/* [R 28] this field hold the last information that caused reserved
   attention. bits [19:0] - address; [22:20] function; [23] reserved;
   [27:24] the master that caused the attention - according to the following
   encodeing:1 = pxp; 2 = mcp; 3 = usdm; 4 = tsdm; 5 = xsdm; 6 = csdm; 7 =
   dbu; 8 = dmae */
#define MISC_REG_GRC_RSV_ATTN					 0xa3c0
/* [R 28] this field hold the last information that caused timeout
   attention. bits [19:0] - address; [22:20] function; [23] reserved;
   [27:24] the master that caused the attention - according to the following
   encodeing:1 = pxp; 2 = mcp; 3 = usdm; 4 = tsdm; 5 = xsdm; 6 = csdm; 7 =
   dbu; 8 = dmae */
#define MISC_REG_GRC_TIMEOUT_ATTN				 0xa3c4
/* [RW 1] Setting this bit enables a timer in the GRC block to timeout any
   access that does not finish within
   ~misc_registers_grc_timout_val.grc_timeout_val cycles. When this bit is
   cleared; this timeout is disabled. If this timeout occurs; the GRC shall
   assert it attention output. */
#define MISC_REG_GRC_TIMEOUT_EN 				 0xa280
/* [RW 28] 28 LSB of LCPLL first register; reset val = 521. inside order of
   the bits is: [2:0] OAC reset value 001) CML output buffer bias control;
   111 for +40%; 011 for +20%; 001 for 0%; 000 for -20%. [5:3] Icp_ctrl
   (reset value 001) Charge pump current control; 111 for 720u; 011 for
   600u; 001 for 480u and 000 for 360u. [7:6] Bias_ctrl (reset value 00)
   Global bias control; When bit 7 is high bias current will be 10 0gh; When
   bit 6 is high bias will be 100w; Valid values are 00; 10; 01. [10:8]
   Pll_observe (reset value 010) Bits to control observability. bit 10 is
   for test bias; bit 9 is for test CK; bit 8 is test Vc. [12:11] Vth_ctrl
   (reset value 00) Comparator threshold control. 00 for 0.6V; 01 for 0.54V
   and 10 for 0.66V. [13] pllSeqStart (reset value 0) Enables VCO tuning
   sequencer: 1= sequencer disabled; 0= sequencer enabled (inverted
   internally). [14] reserved (reset value 0) Reset for VCO sequencer is
   connected to RESET input directly. [15] capRetry_en (reset value 0)
   enable retry on cap search failure (inverted). [16] freqMonitor_e (reset
   value 0) bit to continuously monitor vco freq (inverted). [17]
   freqDetRestart_en (reset value 0) bit to enable restart when not freq
   locked (inverted). [18] freqDetRetry_en (reset value 0) bit to enable
   retry on freq det failure(inverted). [19] pllForceFdone_en (reset value
   0) bit to enable pllForceFdone & pllForceFpass into pllSeq. [20]
   pllForceFdone (reset value 0) bit to force freqDone. [21] pllForceFpass
   (reset value 0) bit to force freqPass. [22] pllForceDone_en (reset value
   0) bit to enable pllForceCapDone. [23] pllForceCapDone (reset value 0)
   bit to force capDone. [24] pllForceCapPass_en (reset value 0) bit to
   enable pllForceCapPass. [25] pllForceCapPass (reset value 0) bit to force
   capPass. [26] capRestart (reset value 0) bit to force cap sequencer to
   restart. [27] capSelectM_en (reset value 0) bit to enable cap select
   register bits. */
#define MISC_REG_LCPLL_CTRL_1					 0xa2a4
#define MISC_REG_LCPLL_CTRL_REG_2				 0xa2a8
/* [RW 1] LCPLL power down. Global register. Active High. Reset on POR
 * reset. */
#define MISC_REG_LCPLL_E40_PWRDWN				 0xaa74
/* [RW 1] LCPLL VCO reset. Global register. Active Low Reset on POR reset. */
#define MISC_REG_LCPLL_E40_RESETB_ANA				 0xaa78
/* [RW 1] LCPLL post-divider reset. Global register. Active Low Reset on POR
 * reset. */
#define MISC_REG_LCPLL_E40_RESETB_DIG				 0xaa7c
/* [RW 4] Interrupt mask register #0 read/write */
#define MISC_REG_MISC_INT_MASK					 0xa388
/* [RW 1] Parity mask register #0 read/write */
#define MISC_REG_MISC_PRTY_MASK 				 0xa398
/* [R 1] Parity register #0 read */
#define MISC_REG_MISC_PRTY_STS					 0xa38c
/* [RC 1] Parity register #0 read clear */
#define MISC_REG_MISC_PRTY_STS_CLR				 0xa390
#define MISC_REG_NIG_WOL_P0					 0xa270
#define MISC_REG_NIG_WOL_P1					 0xa274
/* [R 1] If set indicate that the pcie_rst_b was asserted without perst
   assertion */
#define MISC_REG_PCIE_HOT_RESET 				 0xa618
/* [RW 32] 32 LSB of storm PLL first register; reset val = 0x 071d2911.
   inside order of the bits is: [0] P1 divider[0] (reset value 1); [1] P1
   divider[1] (reset value 0); [2] P1 divider[2] (reset value 0); [3] P1
   divider[3] (reset value 0); [4] P2 divider[0] (reset value 1); [5] P2
   divider[1] (reset value 0); [6] P2 divider[2] (reset value 0); [7] P2
   divider[3] (reset value 0); [8] ph_det_dis (reset value 1); [9]
   freq_det_dis (reset value 0); [10] Icpx[0] (reset value 0); [11] Icpx[1]
   (reset value 1); [12] Icpx[2] (reset value 0); [13] Icpx[3] (reset value
   1); [14] Icpx[4] (reset value 0); [15] Icpx[5] (reset value 0); [16]
   Rx[0] (reset value 1); [17] Rx[1] (reset value 0); [18] vc_en (reset
   value 1); [19] vco_rng[0] (reset value 1); [20] vco_rng[1] (reset value
   1); [21] Kvco_xf[0] (reset value 0); [22] Kvco_xf[1] (reset value 0);
   [23] Kvco_xf[2] (reset value 0); [24] Kvco_xs[0] (reset value 1); [25]
   Kvco_xs[1] (reset value 1); [26] Kvco_xs[2] (reset value 1); [27]
   testd_en (reset value 0); [28] testd_sel[0] (reset value 0); [29]
   testd_sel[1] (reset value 0); [30] testd_sel[2] (reset value 0); [31]
   testa_en (reset value 0); */
#define MISC_REG_PLL_STORM_CTRL_1				 0xa294
#define MISC_REG_PLL_STORM_CTRL_2				 0xa298
#define MISC_REG_PLL_STORM_CTRL_3				 0xa29c
#define MISC_REG_PLL_STORM_CTRL_4				 0xa2a0
/* [R 1] Status of 4 port mode enable input pin. */
#define MISC_REG_PORT4MODE_EN					 0xa750
/* [RW 2] 4 port mode enable overwrite.[0] - Overwrite control; if it is 0 -
 * the port4mode_en output is equal to 4 port mode input pin; if it is 1 -
 * the port4mode_en output is equal to bit[1] of this register; [1] -
 * Overwrite value. If bit[0] of this register is 1 this is the value that
 * receives the port4mode_en output . */
#define MISC_REG_PORT4MODE_EN_OVWR				 0xa720
/* [RW 32] reset reg#2; rite/read one = the specific block is out of reset;
   write/read zero = the specific block is in reset; addr 0-wr- the write
   value will be written to the register; addr 1-set - one will be written
   to all the bits that have the value of one in the data written (bits that
   have the value of zero will not be change) ; addr 2-clear - zero will be
   written to all the bits that have the value of one in the data written
   (bits that have the value of zero will not be change); addr 3-ignore;
   read ignore from all addr except addr 00; inside order of the bits is:
   [0] rst_bmac0; [1] rst_bmac1; [2] rst_emac0; [3] rst_emac1; [4] rst_grc;
   [5] rst_mcp_n_reset_reg_hard_core; [6] rst_ mcp_n_hard_core_rst_b; [7]
   rst_ mcp_n_reset_cmn_cpu; [8] rst_ mcp_n_reset_cmn_core; [9] rst_rbcn;
   [10] rst_dbg; [11] rst_misc_core; [12] rst_dbue (UART); [13]
   Pci_resetmdio_n; [14] rst_emac0_hard_core; [15] rst_emac1_hard_core; 16]
   rst_pxp_rq_rd_wr; 31:17] reserved */
#define MISC_REG_RESET_REG_1					 0xa580
#define MISC_REG_RESET_REG_2					 0xa590
/* [RW 20] 20 bit GRC address where the scratch-pad of the MCP that is
   shared with the driver resides */
#define MISC_REG_SHARED_MEM_ADDR				 0xa2b4
/* [RW 32] SPIO. [31-24] FLOAT When any of these bits is written as a '1';
   the corresponding SPIO bit will turn off it's drivers and become an
   input. This is the reset state of all SPIO pins. The read value of these
   bits will be a '1' if that last command (#SET; #CL; or #FLOAT) for this
   bit was a #FLOAT. (reset value 0xff). [23-16] CLR When any of these bits
   is written as a '1'; the corresponding SPIO bit will drive low. The read
   value of these bits will be a '1' if that last command (#SET; #CLR; or
#FLOAT) for this bit was a #CLR. (reset value 0). [15-8] SET When any of
   these bits is written as a '1'; the corresponding SPIO bit will drive
   high (if it has that capability). The read value of these bits will be a
   '1' if that last command (#SET; #CLR; or #FLOAT) for this bit was a #SET.
   (reset value 0). [7-0] VALUE RO; These bits indicate the read value of
   each of the eight SPIO pins. This is the result value of the pin; not the
   drive value. Writing these bits will have not effect. Each 8 bits field
   is divided as follows: [0] VAUX Enable; when pulsed low; enables supply
   from VAUX. (This is an output pin only; the FLOAT field is not applicable
   for this pin); [1] VAUX Disable; when pulsed low; disables supply form
   VAUX. (This is an output pin only; FLOAT field is not applicable for this
   pin); [2] SEL_VAUX_B - Control to power switching logic. Drive low to
   select VAUX supply. (This is an output pin only; it is not controlled by
   the SET and CLR fields; it is controlled by the Main Power SM; the FLOAT
   field is not applicable for this pin; only the VALUE fields is relevant -
   it reflects the output value); [3] port swap [4] spio_4; [5] spio_5; [6]
   Bit 0 of UMP device ID select; read by UMP firmware; [7] Bit 1 of UMP
   device ID select; read by UMP firmware. */
#define MISC_REG_SPIO						 0xa4fc
/* [RW 8] These bits enable the SPIO_INTs to signals event to the IGU/MC.
   according to the following map: [3:0] reserved; [4] spio_4 [5] spio_5;
   [7:0] reserved */
#define MISC_REG_SPIO_EVENT_EN					 0xa2b8
/* [RW 32] SPIO INT. [31-24] OLD_CLR Writing a '1' to these bit clears the
   corresponding bit in the #OLD_VALUE register. This will acknowledge an
   interrupt on the falling edge of corresponding SPIO input (reset value
   0). [23-16] OLD_SET Writing a '1' to these bit sets the corresponding bit
   in the #OLD_VALUE register. This will acknowledge an interrupt on the
   rising edge of corresponding SPIO input (reset value 0). [15-8] OLD_VALUE
   RO; These bits indicate the old value of the SPIO input value. When the
   ~INT_STATE bit is set; this bit indicates the OLD value of the pin such
   that if ~INT_STATE is set and this bit is '0'; then the interrupt is due
   to a low to high edge. If ~INT_STATE is set and this bit is '1'; then the
   interrupt is due to a high to low edge (reset value 0). [7-0] INT_STATE
   RO; These bits indicate the current SPIO interrupt state for each SPIO
   pin. This bit is cleared when the appropriate #OLD_SET or #OLD_CLR
   command bit is written. This bit is set when the SPIO input does not
   match the current value in #OLD_VALUE (reset value 0). */
#define MISC_REG_SPIO_INT					 0xa500
/* [RW 32] reload value for counter 4 if reload; the value will be reload if
   the counter reached zero and the reload bit
   (~misc_registers_sw_timer_cfg_4.sw_timer_cfg_4[1] ) is set */
#define MISC_REG_SW_TIMER_RELOAD_VAL_4				 0xa2fc
/* [RW 32] the value of the counter for sw timers1-8. there are 8 addresses
   in this register. address 0 - timer 1; address 1 - timer 2, ...  address 7 -
   timer 8 */
#define MISC_REG_SW_TIMER_VAL					 0xa5c0
/* [R 1] Status of two port mode path swap input pin. */
#define MISC_REG_TWO_PORT_PATH_SWAP				 0xa758
/* [RW 2] 2 port swap overwrite.[0] - Overwrite control; if it is 0 - the
   path_swap output is equal to 2 port mode path swap input pin; if it is 1
   - the path_swap output is equal to bit[1] of this register; [1] -
   Overwrite value. If bit[0] of this register is 1 this is the value that
   receives the path_swap output. Reset on Hard reset. */
#define MISC_REG_TWO_PORT_PATH_SWAP_OVWR			 0xa72c
/* [RW 1] Set by the MCP to remember if one or more of the drivers is/are
   loaded; 0-prepare; -unprepare */
#define MISC_REG_UNPREPARED					 0xa424
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_BRCST	 (0x1<<0)
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_MLCST	 (0x1<<1)
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_NO_VLAN	 (0x1<<4)
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_UNCST	 (0x1<<2)
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_VLAN	 (0x1<<3)
/* [RW 5] MDIO PHY Address. The WC uses this address to determine whether or
 * not it is the recipient of the message on the MDIO interface. The value
 * is compared to the value on ctrl_md_devad. Drives output
 * misc_xgxs0_phy_addr. Global register. */
#define MISC_REG_WC0_CTRL_PHY_ADDR				 0xa9cc
#define MISC_REG_WC0_RESET					 0xac30
/* [RW 2] XMAC Core port mode. Indicates the number of ports on the system
   side. This should be less than or equal to phy_port_mode; if some of the
   ports are not used. This enables reduction of frequency on the core side.
   This is a strap input for the XMAC_MP core. 00 - Single Port Mode; 01 -
   Dual Port Mode; 10 - Tri Port Mode; 11 - Quad Port Mode. This is a strap
   input for the XMAC_MP core; and should be changed only while reset is
   held low. Reset on Hard reset. */
#define MISC_REG_XMAC_CORE_PORT_MODE				 0xa964
/* [RW 2] XMAC PHY port mode. Indicates the number of ports on the Warp
   Core. This is a strap input for the XMAC_MP core. 00 - Single Port Mode;
   01 - Dual Port Mode; 1x - Quad Port Mode; This is a strap input for the
   XMAC_MP core; and should be changed only while reset is held low. Reset
   on Hard reset. */
#define MISC_REG_XMAC_PHY_PORT_MODE				 0xa960
/* [RW 32] 1 [47] Packet Size = 64 Write to this register write bits 31:0.
 * Reads from this register will clear bits 31:0. */
#define MSTAT_REG_RX_STAT_GR64_LO				 0x200
/* [RW 32] 1 [00] Tx Good Packet Count Write to this register write bits
 * 31:0. Reads from this register will clear bits 31:0. */
#define MSTAT_REG_TX_STAT_GTXPOK_LO				 0
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_BRCST	 (0x1<<0)
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_MLCST	 (0x1<<1)
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_NO_VLAN	 (0x1<<4)
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_UNCST	 (0x1<<2)
#define NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_VLAN	 (0x1<<3)
#define NIG_LLH0_XCM_MASK_REG_LLH0_XCM_MASK_BCN			 (0x1<<0)
#define NIG_LLH1_XCM_MASK_REG_LLH1_XCM_MASK_BCN			 (0x1<<0)
#define NIG_MASK_INTERRUPT_PORT0_REG_MASK_EMAC0_MISC_MI_INT	 (0x1<<0)
#define NIG_MASK_INTERRUPT_PORT0_REG_MASK_SERDES0_LINK_STATUS	 (0x1<<9)
#define NIG_MASK_INTERRUPT_PORT0_REG_MASK_XGXS0_LINK10G 	 (0x1<<15)
#define NIG_MASK_INTERRUPT_PORT0_REG_MASK_XGXS0_LINK_STATUS	 (0xf<<18)
/* [RW 1] Input enable for RX_BMAC0 IF */
#define NIG_REG_BMAC0_IN_EN					 0x100ac
/* [RW 1] output enable for TX_BMAC0 IF */
#define NIG_REG_BMAC0_OUT_EN					 0x100e0
/* [RW 1] output enable for TX BMAC pause port 0 IF */
#define NIG_REG_BMAC0_PAUSE_OUT_EN				 0x10110
/* [RW 1] output enable for RX_BMAC0_REGS IF */
#define NIG_REG_BMAC0_REGS_OUT_EN				 0x100e8
/* [RW 1] output enable for RX BRB1 port0 IF */
#define NIG_REG_BRB0_OUT_EN					 0x100f8
/* [RW 1] Input enable for TX BRB1 pause port 0 IF */
#define NIG_REG_BRB0_PAUSE_IN_EN				 0x100c4
/* [RW 1] output enable for RX BRB1 port1 IF */
#define NIG_REG_BRB1_OUT_EN					 0x100fc
/* [RW 1] Input enable for TX BRB1 pause port 1 IF */
#define NIG_REG_BRB1_PAUSE_IN_EN				 0x100c8
/* [RW 1] output enable for RX BRB1 LP IF */
#define NIG_REG_BRB_LB_OUT_EN					 0x10100
/* [WB_W 82] Debug packet to LP from RBC; Data spelling:[63:0] data; 64]
   error; [67:65]eop_bvalid; [68]eop; [69]sop; [70]port_id; 71]flush;
   72:73]-vnic_num; 81:74]-sideband_info */
#define NIG_REG_DEBUG_PACKET_LB 				 0x10800
/* [RW 1] Input enable for TX Debug packet */
#define NIG_REG_EGRESS_DEBUG_IN_EN				 0x100dc
/* [RW 1] If 1 - egress drain mode for port0 is active. In this mode all
   packets from PBFare not forwarded to the MAC and just deleted from FIFO.
   First packet may be deleted from the middle. And last packet will be
   always deleted till the end. */
#define NIG_REG_EGRESS_DRAIN0_MODE				 0x10060
/* [RW 1] Output enable to EMAC0 */
#define NIG_REG_EGRESS_EMAC0_OUT_EN				 0x10120
/* [RW 1] MAC configuration for packets of port0. If 1 - all packet outputs
   to emac for port0; other way to bmac for port0 */
#define NIG_REG_EGRESS_EMAC0_PORT				 0x10058
/* [RW 1] Input enable for TX PBF user packet port0 IF */
#define NIG_REG_EGRESS_PBF0_IN_EN				 0x100cc
/* [RW 1] Input enable for TX PBF user packet port1 IF */
#define NIG_REG_EGRESS_PBF1_IN_EN				 0x100d0
/* [RW 1] Input enable for TX UMP management packet port0 IF */
#define NIG_REG_EGRESS_UMP0_IN_EN				 0x100d4
/* [RW 1] Input enable for RX_EMAC0 IF */
#define NIG_REG_EMAC0_IN_EN					 0x100a4
/* [RW 1] output enable for TX EMAC pause port 0 IF */
#define NIG_REG_EMAC0_PAUSE_OUT_EN				 0x10118
/* [R 1] status from emac0. This bit is set when MDINT from either the
   EXT_MDINT pin or from the Copper PHY is driven low. This condition must
   be cleared in the attached PHY device that is driving the MINT pin. */
#define NIG_REG_EMAC0_STATUS_MISC_MI_INT			 0x10494
/* [WB 48] This address space contains BMAC0 registers. The BMAC registers
   are described in appendix A. In order to access the BMAC0 registers; the
   base address; NIG_REGISTERS_INGRESS_BMAC0_MEM; Offset: 0x10c00; should be
   added to each BMAC register offset */
#define NIG_REG_INGRESS_BMAC0_MEM				 0x10c00
/* [WB 48] This address space contains BMAC1 registers. The BMAC registers
   are described in appendix A. In order to access the BMAC0 registers; the
   base address; NIG_REGISTERS_INGRESS_BMAC1_MEM; Offset: 0x11000; should be
   added to each BMAC register offset */
#define NIG_REG_INGRESS_BMAC1_MEM				 0x11000
/* [R 1] FIFO empty in EOP descriptor FIFO of LP in NIG_RX_EOP */
#define NIG_REG_INGRESS_EOP_LB_EMPTY				 0x104e0
/* [RW 17] Debug only. RX_EOP_DSCR_lb_FIFO in NIG_RX_EOP. Data
   packet_length[13:0]; mac_error[14]; trunc_error[15]; parity[16] */
#define NIG_REG_INGRESS_EOP_LB_FIFO				 0x104e4
/* [RW 27] 0 - must be active for Everest A0; 1- for Everest B0 when latch
   logic for interrupts must be used. Enable per bit of interrupt of
   ~latch_status.latch_status */
#define NIG_REG_LATCH_BC_0					 0x16210
/* [RW 27] Latch for each interrupt from Unicore.b[0]
   status_emac0_misc_mi_int; b[1] status_emac0_misc_mi_complete;
   b[2]status_emac0_misc_cfg_change; b[3]status_emac0_misc_link_status;
   b[4]status_emac0_misc_link_change; b[5]status_emac0_misc_attn;
   b[6]status_serdes0_mac_crs; b[7]status_serdes0_autoneg_complete;
   b[8]status_serdes0_fiber_rxact; b[9]status_serdes0_link_status;
   b[10]status_serdes0_mr_page_rx; b[11]status_serdes0_cl73_an_complete;
   b[12]status_serdes0_cl73_mr_page_rx; b[13]status_serdes0_rx_sigdet;
   b[14]status_xgxs0_remotemdioreq; b[15]status_xgxs0_link10g;
   b[16]status_xgxs0_autoneg_complete; b[17]status_xgxs0_fiber_rxact;
   b[21:18]status_xgxs0_link_status; b[22]status_xgxs0_mr_page_rx;
   b[23]status_xgxs0_cl73_an_complete; b[24]status_xgxs0_cl73_mr_page_rx;
   b[25]status_xgxs0_rx_sigdet; b[26]status_xgxs0_mac_crs */
#define NIG_REG_LATCH_STATUS_0					 0x18000
/* [RW 1] led 10g for port 0 */
#define NIG_REG_LED_10G_P0					 0x10320
/* [RW 1] led 10g for port 1 */
#define NIG_REG_LED_10G_P1					 0x10324
/* [RW 1] Port0: This bit is set to enable the use of the
   ~nig_registers_led_control_blink_rate_p0.led_control_blink_rate_p0 field
   defined below. If this bit is cleared; then the blink rate will be about
   8Hz. */
#define NIG_REG_LED_CONTROL_BLINK_RATE_ENA_P0			 0x10318
/* [RW 12] Port0: Specifies the period of each blink cycle (on + off) for
   Traffic LED in milliseconds. Must be a non-zero value. This 12-bit field
   is reset to 0x080; giving a default blink period of approximately 8Hz. */
#define NIG_REG_LED_CONTROL_BLINK_RATE_P0			 0x10310
/* [RW 1] Port0: If set along with the
 ~nig_registers_led_control_override_traffic_p0.led_control_override_traffic_p0
   bit and ~nig_registers_led_control_traffic_p0.led_control_traffic_p0 LED
   bit; the Traffic LED will blink with the blink rate specified in
   ~nig_registers_led_control_blink_rate_p0.led_control_blink_rate_p0 and
   ~nig_registers_led_control_blink_rate_ena_p0.led_control_blink_rate_ena_p0
   fields. */
#define NIG_REG_LED_CONTROL_BLINK_TRAFFIC_P0			 0x10308
/* [RW 1] Port0: If set overrides hardware control of the Traffic LED. The
   Traffic LED will then be controlled via bit ~nig_registers_
   led_control_traffic_p0.led_control_traffic_p0 and bit
   ~nig_registers_led_control_blink_traffic_p0.led_control_blink_traffic_p0 */
#define NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0 		 0x102f8
/* [RW 1] Port0: If set along with the led_control_override_trafic_p0 bit;
   turns on the Traffic LED. If the led_control_blink_traffic_p0 bit is also
   set; the LED will blink with blink rate specified in
   ~nig_registers_led_control_blink_rate_p0.led_control_blink_rate_p0 and
   ~nig_regsters_led_control_blink_rate_ena_p0.led_control_blink_rate_ena_p0
   fields. */
#define NIG_REG_LED_CONTROL_TRAFFIC_P0				 0x10300
/* [RW 4] led mode for port0: 0 MAC; 1-3 PHY1; 4 MAC2; 5-7 PHY4; 8-MAC3;
   9-11PHY7; 12 MAC4; 13-15 PHY10; */
#define NIG_REG_LED_MODE_P0					 0x102f0
/* [RW 3] for port0 enable for llfc ppp and pause. b0 - brb1 enable; b1-
   tsdm enable; b2- usdm enable */
#define NIG_REG_LLFC_EGRESS_SRC_ENABLE_0			 0x16070
#define NIG_REG_LLFC_EGRESS_SRC_ENABLE_1			 0x16074
/* [RW 1] SAFC enable for port0. This register may get 1 only when
   ~ppp_enable.ppp_enable = 0 and pause_enable.pause_enable =0 for the same
   port */
#define NIG_REG_LLFC_ENABLE_0					 0x16208
#define NIG_REG_LLFC_ENABLE_1					 0x1620c
/* [RW 16] classes are high-priority for port0 */
#define NIG_REG_LLFC_HIGH_PRIORITY_CLASSES_0			 0x16058
#define NIG_REG_LLFC_HIGH_PRIORITY_CLASSES_1			 0x1605c
/* [RW 16] classes are low-priority for port0 */
#define NIG_REG_LLFC_LOW_PRIORITY_CLASSES_0			 0x16060
#define NIG_REG_LLFC_LOW_PRIORITY_CLASSES_1			 0x16064
/* [RW 1] Output enable of message to LLFC BMAC IF for port0 */
#define NIG_REG_LLFC_OUT_EN_0					 0x160c8
#define NIG_REG_LLFC_OUT_EN_1					 0x160cc
#define NIG_REG_LLH0_ACPI_PAT_0_CRC				 0x1015c
#define NIG_REG_LLH0_ACPI_PAT_6_LEN				 0x10154
#define NIG_REG_LLH0_BRB1_DRV_MASK				 0x10244
#define NIG_REG_LLH0_BRB1_DRV_MASK_MF				 0x16048
/* [RW 1] send to BRB1 if no match on any of RMP rules. */
#define NIG_REG_LLH0_BRB1_NOT_MCP				 0x1025c
/* [RW 2] Determine the classification participants. 0: no classification.1:
   classification upon VLAN id. 2: classification upon MAC address. 3:
   classification upon both VLAN id & MAC addr. */
#define NIG_REG_LLH0_CLS_TYPE					 0x16080
/* [RW 32] cm header for llh0 */
#define NIG_REG_LLH0_CM_HEADER					 0x1007c
#define NIG_REG_LLH0_DEST_IP_0_1				 0x101dc
#define NIG_REG_LLH0_DEST_MAC_0_0				 0x101c0
/* [RW 16] destination TCP address 1. The LLH will look for this address in
   all incoming packets. */
#define NIG_REG_LLH0_DEST_TCP_0 				 0x10220
/* [RW 16] destination UDP address 1 The LLH will look for this address in
   all incoming packets. */
#define NIG_REG_LLH0_DEST_UDP_0 				 0x10214
#define NIG_REG_LLH0_ERROR_MASK 				 0x1008c
/* [RW 8] event id for llh0 */
#define NIG_REG_LLH0_EVENT_ID					 0x10084
#define NIG_REG_LLH0_FUNC_EN					 0x160fc
#define NIG_REG_LLH0_FUNC_MEM					 0x16180
#define NIG_REG_LLH0_FUNC_MEM_ENABLE				 0x16140
#define NIG_REG_LLH0_FUNC_VLAN_ID				 0x16100
/* [RW 1] Determine the IP version to look for in
   ~nig_registers_llh0_dest_ip_0.llh0_dest_ip_0. 0 - IPv6; 1-IPv4 */
#define NIG_REG_LLH0_IPV4_IPV6_0				 0x10208
/* [RW 1] t bit for llh0 */
#define NIG_REG_LLH0_T_BIT					 0x10074
/* [RW 12] VLAN ID 1. In case of VLAN packet the LLH will look for this ID. */
#define NIG_REG_LLH0_VLAN_ID_0					 0x1022c
/* [RW 8] init credit counter for port0 in LLH */
#define NIG_REG_LLH0_XCM_INIT_CREDIT				 0x10554
#define NIG_REG_LLH0_XCM_MASK					 0x10130
#define NIG_REG_LLH1_BRB1_DRV_MASK				 0x10248
/* [RW 1] send to BRB1 if no match on any of RMP rules. */
#define NIG_REG_LLH1_BRB1_NOT_MCP				 0x102dc
/* [RW 2] Determine the classification participants. 0: no classification.1:
   classification upon VLAN id. 2: classification upon MAC address. 3:
   classification upon both VLAN id & MAC addr. */
#define NIG_REG_LLH1_CLS_TYPE					 0x16084
/* [RW 32] cm header for llh1 */
#define NIG_REG_LLH1_CM_HEADER					 0x10080
#define NIG_REG_LLH1_ERROR_MASK 				 0x10090
/* [RW 8] event id for llh1 */
#define NIG_REG_LLH1_EVENT_ID					 0x10088
#define NIG_REG_LLH1_FUNC_MEM					 0x161c0
#define NIG_REG_LLH1_FUNC_MEM_ENABLE				 0x16160
#define NIG_REG_LLH1_FUNC_MEM_SIZE				 16
/* [RW 1] When this bit is set; the LLH will classify the packet before
 * sending it to the BRB or calculating WoL on it. This bit controls port 1
 * only. The legacy llh_multi_function_mode bit controls port 0. */
#define NIG_REG_LLH1_MF_MODE					 0x18614
/* [RW 8] init credit counter for port1 in LLH */
#define NIG_REG_LLH1_XCM_INIT_CREDIT				 0x10564
#define NIG_REG_LLH1_XCM_MASK					 0x10134
/* [RW 1] When this bit is set; the LLH will expect all packets to be with
   e1hov */
#define NIG_REG_LLH_E1HOV_MODE					 0x160d8
/* [RW 1] When this bit is set; the LLH will classify the packet before
   sending it to the BRB or calculating WoL on it. */
#define NIG_REG_LLH_MF_MODE					 0x16024
#define NIG_REG_MASK_INTERRUPT_PORT0				 0x10330
#define NIG_REG_MASK_INTERRUPT_PORT1				 0x10334
/* [RW 1] Output signal from NIG to EMAC0. When set enables the EMAC0 block. */
#define NIG_REG_NIG_EMAC0_EN					 0x1003c
/* [RW 1] Output signal from NIG to EMAC1. When set enables the EMAC1 block. */
#define NIG_REG_NIG_EMAC1_EN					 0x10040
/* [RW 1] Output signal from NIG to TX_EMAC0. When set indicates to the
   EMAC0 to strip the CRC from the ingress packets. */
#define NIG_REG_NIG_INGRESS_EMAC0_NO_CRC			 0x10044
/* [R 32] Interrupt register #0 read */
#define NIG_REG_NIG_INT_STS_0					 0x103b0
#define NIG_REG_NIG_INT_STS_1					 0x103c0
/* [R 32] Legacy E1 and E1H location for parity error mask register. */
#define NIG_REG_NIG_PRTY_MASK					 0x103dc
/* [RW 32] Parity mask register #0 read/write */
#define NIG_REG_NIG_PRTY_MASK_0					 0x183c8
#define NIG_REG_NIG_PRTY_MASK_1					 0x183d8
/* [R 32] Legacy E1 and E1H location for parity error status register. */
#define NIG_REG_NIG_PRTY_STS					 0x103d0
/* [R 32] Parity register #0 read */
#define NIG_REG_NIG_PRTY_STS_0					 0x183bc
#define NIG_REG_NIG_PRTY_STS_1					 0x183cc
/* [R 32] Legacy E1 and E1H location for parity error status clear register. */
#define NIG_REG_NIG_PRTY_STS_CLR				 0x103d4
/* [RC 32] Parity register #0 read clear */
#define NIG_REG_NIG_PRTY_STS_CLR_0				 0x183c0
#define NIG_REG_NIG_PRTY_STS_CLR_1				 0x183d0
#define MCPR_IMC_COMMAND_ENABLE					 (1L<<31)
#define MCPR_IMC_COMMAND_IMC_STATUS_BITSHIFT			 16
#define MCPR_IMC_COMMAND_OPERATION_BITSHIFT			 28
#define MCPR_IMC_COMMAND_TRANSFER_ADDRESS_BITSHIFT		 8
/* [RW 6] Bit-map indicating which L2 hdrs may appear after the basic
 * Ethernet header. */
#define NIG_REG_P0_HDRS_AFTER_BASIC				 0x18038
/* [RW 1] HW PFC enable bit. Set this bit to enable the PFC functionality in
 * the NIG. Other flow control modes such as PAUSE and SAFC/LLFC should be
 * disabled when this bit is set. */
#define NIG_REG_P0_HWPFC_ENABLE				 0x18078
#define NIG_REG_P0_LLH_FUNC_MEM2				 0x18480
#define NIG_REG_P0_LLH_FUNC_MEM2_ENABLE			 0x18440
/* [RW 1] Input enable for RX MAC interface. */
#define NIG_REG_P0_MAC_IN_EN					 0x185ac
/* [RW 1] Output enable for TX MAC interface */
#define NIG_REG_P0_MAC_OUT_EN					 0x185b0
/* [RW 1] Output enable for TX PAUSE signal to the MAC. */
#define NIG_REG_P0_MAC_PAUSE_OUT_EN				 0x185b4
/* [RW 32] Eight 4-bit configurations for specifying which COS (0-15 for
 * future expansion) each priorty is to be mapped to. Bits 3:0 specify the
 * COS for priority 0. Bits 31:28 specify the COS for priority 7. The 3-bit
 * priority field is extracted from the outer-most VLAN in receive packet.
 * Only COS 0 and COS 1 are supported in E2. */
#define NIG_REG_P0_PKT_PRIORITY_TO_COS				 0x18054
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 0. A
 * priority is mapped to COS 0 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P0_RX_COS0_PRIORITY_MASK			 0x18058
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 1. A
 * priority is mapped to COS 1 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P0_RX_COS1_PRIORITY_MASK			 0x1805c
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 2. A
 * priority is mapped to COS 2 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P0_RX_COS2_PRIORITY_MASK			 0x186b0
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 3. A
 * priority is mapped to COS 3 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P0_RX_COS3_PRIORITY_MASK			 0x186b4
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 4. A
 * priority is mapped to COS 4 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P0_RX_COS4_PRIORITY_MASK			 0x186b8
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 5. A
 * priority is mapped to COS 5 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P0_RX_COS5_PRIORITY_MASK			 0x186bc
/* [R 1] RX FIFO for receiving data from MAC is empty. */
/* [RW 15] Specify which of the credit registers the client is to be mapped
 * to. Bits[2:0] are for client 0; bits [14:12] are for client 4. For
 * clients that are not subject to WFQ credit blocking - their
 * specifications here are not used. */
#define NIG_REG_P0_TX_ARB_CLIENT_CREDIT_MAP			 0x180f0
/* [RW 32] Specify which of the credit registers the client is to be mapped
 * to. This register specifies bits 31:0 of the 36-bit value. Bits[3:0] are
 * for client 0; bits [35:32] are for client 8. For clients that are not
 * subject to WFQ credit blocking - their specifications here are not used.
 * This is a new register (with 2_) added in E3 B0 to accommodate the 9
 * input clients to ETS arbiter. The reset default is set for management and
 * debug to use credit registers 6, 7, and 8, respectively, and COSes 0-5 to
 * use credit registers 0-5 respectively (0x543210876). Note that credit
 * registers can not be shared between clients. */
#define NIG_REG_P0_TX_ARB_CLIENT_CREDIT_MAP2_LSB		 0x18688
/* [RW 4] Specify which of the credit registers the client is to be mapped
 * to. This register specifies bits 35:32 of the 36-bit value. Bits[3:0] are
 * for client 0; bits [35:32] are for client 8. For clients that are not
 * subject to WFQ credit blocking - their specifications here are not used.
 * This is a new register (with 2_) added in E3 B0 to accommodate the 9
 * input clients to ETS arbiter. The reset default is set for management and
 * debug to use credit registers 6, 7, and 8, respectively, and COSes 0-5 to
 * use credit registers 0-5 respectively (0x543210876). Note that credit
 * registers can not be shared between clients. */
#define NIG_REG_P0_TX_ARB_CLIENT_CREDIT_MAP2_MSB		 0x1868c
/* [RW 5] Specify whether the client competes directly in the strict
 * priority arbiter. The bits are mapped according to client ID (client IDs
 * are defined in tx_arb_priority_client). Default value is set to enable
 * strict priorities for clients 0-2 -- management and debug traffic. */
#define NIG_REG_P0_TX_ARB_CLIENT_IS_STRICT			 0x180e8
/* [RW 5] Specify whether the client is subject to WFQ credit blocking. The
 * bits are mapped according to client ID (client IDs are defined in
 * tx_arb_priority_client). Default value is 0 for not using WFQ credit
 * blocking. */
#define NIG_REG_P0_TX_ARB_CLIENT_IS_SUBJECT2WFQ		 0x180ec
/* [RW 32] Specify the upper bound that credit register 0 is allowed to
 * reach. */
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_0			 0x1810c
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_1			 0x18110
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_2			 0x18114
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_3			 0x18118
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_4			 0x1811c
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_5			 0x186a0
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_6			 0x186a4
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_7			 0x186a8
#define NIG_REG_P0_TX_ARB_CREDIT_UPPER_BOUND_8			 0x186ac
/* [RW 32] Specify the weight (in bytes) to be added to credit register 0
 * when it is time to increment. */
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_0			 0x180f8
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_1			 0x180fc
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_2			 0x18100
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_3			 0x18104
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_4			 0x18108
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_5			 0x18690
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_6			 0x18694
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_7			 0x18698
#define NIG_REG_P0_TX_ARB_CREDIT_WEIGHT_8			 0x1869c
/* [RW 12] Specify the number of strict priority arbitration slots between
 * two round-robin arbitration slots to avoid starvation. A value of 0 means
 * no strict priority cycles - the strict priority with anti-starvation
 * arbiter becomes a round-robin arbiter. */
#define NIG_REG_P0_TX_ARB_NUM_STRICT_ARB_SLOTS			 0x180f4
/* [RW 15] Specify the client number to be assigned to each priority of the
 * strict priority arbiter. Priority 0 is the highest priority. Bits [2:0]
 * are for priority 0 client; bits [14:12] are for priority 4 client. The
 * clients are assigned the following IDs: 0-management; 1-debug traffic
 * from this port; 2-debug traffic from other port; 3-COS0 traffic; 4-COS1
 * traffic. The reset value[14:0] is set to 0x4688 (15'b100_011_010_001_000)
 * for management at priority 0; debug traffic at priorities 1 and 2; COS0
 * traffic at priority 3; and COS1 traffic at priority 4. */
#define NIG_REG_P0_TX_ARB_PRIORITY_CLIENT			 0x180e4
/* [RW 6] Bit-map indicating which L2 hdrs may appear after the basic
 * Ethernet header. */
#define NIG_REG_P1_HDRS_AFTER_BASIC				 0x1818c
#define NIG_REG_P1_LLH_FUNC_MEM2				 0x184c0
#define NIG_REG_P1_LLH_FUNC_MEM2_ENABLE			 0x18460
/* [RW 32] Specify the client number to be assigned to each priority of the
 * strict priority arbiter. This register specifies bits 31:0 of the 36-bit
 * value. Priority 0 is the highest priority. Bits [3:0] are for priority 0
 * client; bits [35-32] are for priority 8 client. The clients are assigned
 * the following IDs: 0-management; 1-debug traffic from this port; 2-debug
 * traffic from other port; 3-COS0 traffic; 4-COS1 traffic; 5-COS2 traffic;
 * 6-COS3 traffic; 7-COS4 traffic; 8-COS5 traffic. The reset value[35:0] is
 * set to 0x345678021. This is a new register (with 2_) added in E3 B0 to
 * accommodate the 9 input clients to ETS arbiter. */
#define NIG_REG_P0_TX_ARB_PRIORITY_CLIENT2_LSB			 0x18680
/* [RW 4] Specify the client number to be assigned to each priority of the
 * strict priority arbiter. This register specifies bits 35:32 of the 36-bit
 * value. Priority 0 is the highest priority. Bits [3:0] are for priority 0
 * client; bits [35-32] are for priority 8 client. The clients are assigned
 * the following IDs: 0-management; 1-debug traffic from this port; 2-debug
 * traffic from other port; 3-COS0 traffic; 4-COS1 traffic; 5-COS2 traffic;
 * 6-COS3 traffic; 7-COS4 traffic; 8-COS5 traffic. The reset value[35:0] is
 * set to 0x345678021. This is a new register (with 2_) added in E3 B0 to
 * accommodate the 9 input clients to ETS arbiter. */
#define NIG_REG_P0_TX_ARB_PRIORITY_CLIENT2_MSB			 0x18684
#define NIG_REG_P1_HWPFC_ENABLE					 0x181d0
#define NIG_REG_P1_MAC_IN_EN					 0x185c0
/* [RW 1] Output enable for TX MAC interface */
#define NIG_REG_P1_MAC_OUT_EN					 0x185c4
/* [RW 1] Output enable for TX PAUSE signal to the MAC. */
#define NIG_REG_P1_MAC_PAUSE_OUT_EN				 0x185c8
/* [RW 32] Eight 4-bit configurations for specifying which COS (0-15 for
 * future expansion) each priorty is to be mapped to. Bits 3:0 specify the
 * COS for priority 0. Bits 31:28 specify the COS for priority 7. The 3-bit
 * priority field is extracted from the outer-most VLAN in receive packet.
 * Only COS 0 and COS 1 are supported in E2. */
#define NIG_REG_P1_PKT_PRIORITY_TO_COS				 0x181a8
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 0. A
 * priority is mapped to COS 0 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P1_RX_COS0_PRIORITY_MASK			 0x181ac
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 1. A
 * priority is mapped to COS 1 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P1_RX_COS1_PRIORITY_MASK			 0x181b0
/* [RW 16] Bit-map indicating which SAFC/PFC priorities to map to COS 2. A
 * priority is mapped to COS 2 when the corresponding mask bit is 1. More
 * than one bit may be set; allowing multiple priorities to be mapped to one
 * COS. */
#define NIG_REG_P1_RX_COS2_PRIORITY_MASK			 0x186f8
/* [R 1] RX FIFO for receiving data from MAC is empty. */
#define NIG_REG_P1_RX_MACFIFO_EMPTY				 0x1858c
/* [R 1] TLLH FIFO is empty. */
#define NIG_REG_P1_TLLH_FIFO_EMPTY				 0x18338
/* [RW 32] Specify which of the credit registers the client is to be mapped
 * to. This register specifies bits 31:0 of the 36-bit value. Bits[3:0] are
 * for client 0; bits [35:32] are for client 8. For clients that are not
 * subject to WFQ credit blocking - their specifications here are not used.
 * This is a new register (with 2_) added in E3 B0 to accommodate the 9
 * input clients to ETS arbiter. The reset default is set for management and
 * debug to use credit registers 6, 7, and 8, respectively, and COSes 0-5 to
 * use credit registers 0-5 respectively (0x543210876). Note that credit
 * registers can not be shared between clients. Note also that there are
 * only COS0-2 in port 1- there is a total of 6 clients in port 1. Only
 * credit registers 0-5 are valid. This register should be configured
 * appropriately before enabling WFQ. */
#define NIG_REG_P1_TX_ARB_CLIENT_CREDIT_MAP2_LSB		 0x186e8
/* [RW 4] Specify which of the credit registers the client is to be mapped
 * to. This register specifies bits 35:32 of the 36-bit value. Bits[3:0] are
 * for client 0; bits [35:32] are for client 8. For clients that are not
 * subject to WFQ credit blocking - their specifications here are not used.
 * This is a new register (with 2_) added in E3 B0 to accommodate the 9
 * input clients to ETS arbiter. The reset default is set for management and
 * debug to use credit registers 6, 7, and 8, respectively, and COSes 0-5 to
 * use credit registers 0-5 respectively (0x543210876). Note that credit
 * registers can not be shared between clients. Note also that there are
 * only COS0-2 in port 1- there is a total of 6 clients in port 1. Only
 * credit registers 0-5 are valid. This register should be configured
 * appropriately before enabling WFQ. */
#define NIG_REG_P1_TX_ARB_CLIENT_CREDIT_MAP2_MSB		 0x186ec
/* [RW 9] Specify whether the client competes directly in the strict
 * priority arbiter. The bits are mapped according to client ID (client IDs
 * are defined in tx_arb_priority_client2): 0-management; 1-debug traffic
 * from this port; 2-debug traffic from other port; 3-COS0 traffic; 4-COS1
 * traffic; 5-COS2 traffic; 6-COS3 traffic; 7-COS4 traffic; 8-COS5 traffic.
 * Default value is set to enable strict priorities for all clients. */
#define NIG_REG_P1_TX_ARB_CLIENT_IS_STRICT			 0x18234
/* [RW 9] Specify whether the client is subject to WFQ credit blocking. The
 * bits are mapped according to client ID (client IDs are defined in
 * tx_arb_priority_client2): 0-management; 1-debug traffic from this port;
 * 2-debug traffic from other port; 3-COS0 traffic; 4-COS1 traffic; 5-COS2
 * traffic; 6-COS3 traffic; 7-COS4 traffic; 8-COS5 traffic. Default value is
 * 0 for not using WFQ credit blocking. */
#define NIG_REG_P1_TX_ARB_CLIENT_IS_SUBJECT2WFQ			 0x18238
#define NIG_REG_P1_TX_ARB_CREDIT_UPPER_BOUND_0			 0x18258
#define NIG_REG_P1_TX_ARB_CREDIT_UPPER_BOUND_1			 0x1825c
#define NIG_REG_P1_TX_ARB_CREDIT_UPPER_BOUND_2			 0x18260
#define NIG_REG_P1_TX_ARB_CREDIT_UPPER_BOUND_3			 0x18264
#define NIG_REG_P1_TX_ARB_CREDIT_UPPER_BOUND_4			 0x18268
#define NIG_REG_P1_TX_ARB_CREDIT_UPPER_BOUND_5			 0x186f4
/* [RW 32] Specify the weight (in bytes) to be added to credit register 0
 * when it is time to increment. */
#define NIG_REG_P1_TX_ARB_CREDIT_WEIGHT_0			 0x18244
#define NIG_REG_P1_TX_ARB_CREDIT_WEIGHT_1			 0x18248
#define NIG_REG_P1_TX_ARB_CREDIT_WEIGHT_2			 0x1824c
#define NIG_REG_P1_TX_ARB_CREDIT_WEIGHT_3			 0x18250
#define NIG_REG_P1_TX_ARB_CREDIT_WEIGHT_4			 0x18254
#define NIG_REG_P1_TX_ARB_CREDIT_WEIGHT_5			 0x186f0
/* [RW 12] Specify the number of strict priority arbitration slots between
   two round-robin arbitration slots to avoid starvation. A value of 0 means
   no strict priority cycles - the strict priority with anti-starvation
   arbiter becomes a round-robin arbiter. */
#define NIG_REG_P1_TX_ARB_NUM_STRICT_ARB_SLOTS			 0x18240
/* [RW 32] Specify the client number to be assigned to each priority of the
   strict priority arbiter. This register specifies bits 31:0 of the 36-bit
   value. Priority 0 is the highest priority. Bits [3:0] are for priority 0
   client; bits [35-32] are for priority 8 client. The clients are assigned
   the following IDs: 0-management; 1-debug traffic from this port; 2-debug
   traffic from other port; 3-COS0 traffic; 4-COS1 traffic; 5-COS2 traffic;
   6-COS3 traffic; 7-COS4 traffic; 8-COS5 traffic. The reset value[35:0] is
   set to 0x345678021. This is a new register (with 2_) added in E3 B0 to
   accommodate the 9 input clients to ETS arbiter. Note that this register
   is the same as the one for port 0, except that port 1 only has COS 0-2
   traffic. There is no traffic for COS 3-5 of port 1. */
#define NIG_REG_P1_TX_ARB_PRIORITY_CLIENT2_LSB			 0x186e0
/* [RW 4] Specify the client number to be assigned to each priority of the
   strict priority arbiter. This register specifies bits 35:32 of the 36-bit
   value. Priority 0 is the highest priority. Bits [3:0] are for priority 0
   client; bits [35-32] are for priority 8 client. The clients are assigned
   the following IDs: 0-management; 1-debug traffic from this port; 2-debug
   traffic from other port; 3-COS0 traffic; 4-COS1 traffic; 5-COS2 traffic;
   6-COS3 traffic; 7-COS4 traffic; 8-COS5 traffic. The reset value[35:0] is
   set to 0x345678021. This is a new register (with 2_) added in E3 B0 to
   accommodate the 9 input clients to ETS arbiter. Note that this register
   is the same as the one for port 0, except that port 1 only has COS 0-2
   traffic. There is no traffic for COS 3-5 of port 1. */
#define NIG_REG_P1_TX_ARB_PRIORITY_CLIENT2_MSB			 0x186e4
/* [R 1] TX FIFO for transmitting data to MAC is empty. */
#define NIG_REG_P1_TX_MACFIFO_EMPTY				 0x18594
/* [R 1] FIFO empty status of the MCP TX FIFO used for storing MCP packets
   forwarded to the host. */
#define NIG_REG_P1_TX_MNG_HOST_FIFO_EMPTY			 0x182b8
/* [RW 32] Specify the upper bound that credit register 0 is allowed to
 * reach. */
/* [RW 1] Pause enable for port0. This register may get 1 only when
   ~safc_enable.safc_enable = 0 and ppp_enable.ppp_enable =0 for the same
   port */
#define NIG_REG_PAUSE_ENABLE_0					 0x160c0
#define NIG_REG_PAUSE_ENABLE_1					 0x160c4
/* [RW 1] Input enable for RX PBF LP IF */
#define NIG_REG_PBF_LB_IN_EN					 0x100b4
/* [RW 1] Value of this register will be transmitted to port swap when
   ~nig_registers_strap_override.strap_override =1 */
#define NIG_REG_PORT_SWAP					 0x10394
/* [RW 1] PPP enable for port0. This register may get 1 only when
 * ~safc_enable.safc_enable = 0 and pause_enable.pause_enable =0 for the
 * same port */
#define NIG_REG_PPP_ENABLE_0					 0x160b0
#define NIG_REG_PPP_ENABLE_1					 0x160b4
/* [RW 1] output enable for RX parser descriptor IF */
#define NIG_REG_PRS_EOP_OUT_EN					 0x10104
/* [RW 1] Input enable for RX parser request IF */
#define NIG_REG_PRS_REQ_IN_EN					 0x100b8
/* [RW 5] control to serdes - CL45 DEVAD */
#define NIG_REG_SERDES0_CTRL_MD_DEVAD				 0x10370
/* [RW 1] control to serdes; 0 - clause 45; 1 - clause 22 */
#define NIG_REG_SERDES0_CTRL_MD_ST				 0x1036c
/* [RW 5] control to serdes - CL22 PHY_ADD and CL45 PRTAD */
#define NIG_REG_SERDES0_CTRL_PHY_ADDR				 0x10374
/* [R 1] status from serdes0 that inputs to interrupt logic of link status */
#define NIG_REG_SERDES0_STATUS_LINK_STATUS			 0x10578
/* [R 32] Rx statistics : In user packets discarded due to BRB backpressure
   for port0 */
#define NIG_REG_STAT0_BRB_DISCARD				 0x105f0
/* [R 32] Rx statistics : In user packets truncated due to BRB backpressure
   for port0 */
#define NIG_REG_STAT0_BRB_TRUNCATE				 0x105f8
/* [WB_R 36] Tx statistics : Number of packets from emac0 or bmac0 that
   between 1024 and 1522 bytes for port0 */
#define NIG_REG_STAT0_EGRESS_MAC_PKT0				 0x10750
/* [WB_R 36] Tx statistics : Number of packets from emac0 or bmac0 that
   between 1523 bytes and above for port0 */
#define NIG_REG_STAT0_EGRESS_MAC_PKT1				 0x10760
/* [R 32] Rx statistics : In user packets discarded due to BRB backpressure
   for port1 */
#define NIG_REG_STAT1_BRB_DISCARD				 0x10628
/* [WB_R 36] Tx statistics : Number of packets from emac1 or bmac1 that
   between 1024 and 1522 bytes for port1 */
#define NIG_REG_STAT1_EGRESS_MAC_PKT0				 0x107a0
/* [WB_R 36] Tx statistics : Number of packets from emac1 or bmac1 that
   between 1523 bytes and above for port1 */
#define NIG_REG_STAT1_EGRESS_MAC_PKT1				 0x107b0
/* [WB_R 64] Rx statistics : User octets received for LP */
#define NIG_REG_STAT2_BRB_OCTET 				 0x107e0
#define NIG_REG_STATUS_INTERRUPT_PORT0				 0x10328
#define NIG_REG_STATUS_INTERRUPT_PORT1				 0x1032c
/* [RW 1] port swap mux selection. If this register equal to 0 then port
   swap is equal to SPIO pin that inputs from ifmux_serdes_swap. If 1 then
   ort swap is equal to ~nig_registers_port_swap.port_swap */
#define NIG_REG_STRAP_OVERRIDE					 0x10398
/* [RW 1] output enable for RX_XCM0 IF */
#define NIG_REG_XCM0_OUT_EN					 0x100f0
/* [RW 1] output enable for RX_XCM1 IF */
#define NIG_REG_XCM1_OUT_EN					 0x100f4
/* [RW 1] control to xgxs - remote PHY in-band MDIO */
#define NIG_REG_XGXS0_CTRL_EXTREMOTEMDIOST			 0x10348
/* [RW 5] control to xgxs - CL45 DEVAD */
#define NIG_REG_XGXS0_CTRL_MD_DEVAD				 0x1033c
/* [RW 1] control to xgxs; 0 - clause 45; 1 - clause 22 */
#define NIG_REG_XGXS0_CTRL_MD_ST				 0x10338
/* [RW 5] control to xgxs - CL22 PHY_ADD and CL45 PRTAD */
#define NIG_REG_XGXS0_CTRL_PHY_ADDR				 0x10340
/* [R 1] status from xgxs0 that inputs to interrupt logic of link10g. */
#define NIG_REG_XGXS0_STATUS_LINK10G				 0x10680
/* [R 4] status from xgxs0 that inputs to interrupt logic of link status */
#define NIG_REG_XGXS0_STATUS_LINK_STATUS			 0x10684
/* [RW 2] selection for XGXS lane of port 0 in NIG_MUX block */
#define NIG_REG_XGXS_LANE_SEL_P0				 0x102e8
/* [RW 1] selection for port0 for NIG_MUX block : 0 = SerDes; 1 = XGXS */
#define NIG_REG_XGXS_SERDES0_MODE_SEL				 0x102e0
#define NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_MI_INT  (0x1<<0)
#define NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_LINK_STATUS (0x1<<9)
#define NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK10G	 (0x1<<15)
#define NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK_STATUS  (0xf<<18)
#define NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK_STATUS_SIZE 18
/* [RW 31] The upper bound of the weight of COS0 in the ETS command arbiter. */
#define PBF_REG_COS0_UPPER_BOUND				 0x15c05c
/* [RW 31] The upper bound of the weight of COS0 in the ETS command arbiter
 * of port 0. */
#define PBF_REG_COS0_UPPER_BOUND_P0				 0x15c2cc
/* [RW 31] The upper bound of the weight of COS0 in the ETS command arbiter
 * of port 1. */
#define PBF_REG_COS0_UPPER_BOUND_P1				 0x15c2e4
/* [RW 31] The weight of COS0 in the ETS command arbiter. */
#define PBF_REG_COS0_WEIGHT					 0x15c054
/* [RW 31] The weight of COS0 in port 0 ETS command arbiter. */
#define PBF_REG_COS0_WEIGHT_P0					 0x15c2a8
/* [RW 31] The weight of COS0 in port 1 ETS command arbiter. */
#define PBF_REG_COS0_WEIGHT_P1					 0x15c2c0
/* [RW 31] The upper bound of the weight of COS1 in the ETS command arbiter. */
#define PBF_REG_COS1_UPPER_BOUND				 0x15c060
/* [RW 31] The weight of COS1 in the ETS command arbiter. */
#define PBF_REG_COS1_WEIGHT					 0x15c058
/* [RW 31] The weight of COS1 in port 0 ETS command arbiter. */
#define PBF_REG_COS1_WEIGHT_P0					 0x15c2ac
/* [RW 31] The weight of COS1 in port 1 ETS command arbiter. */
#define PBF_REG_COS1_WEIGHT_P1					 0x15c2c4
/* [RW 31] The weight of COS2 in port 0 ETS command arbiter. */
#define PBF_REG_COS2_WEIGHT_P0					 0x15c2b0
/* [RW 31] The weight of COS2 in port 1 ETS command arbiter. */
#define PBF_REG_COS2_WEIGHT_P1					 0x15c2c8
/* [RW 31] The weight of COS3 in port 0 ETS command arbiter. */
#define PBF_REG_COS3_WEIGHT_P0					 0x15c2b4
/* [RW 31] The weight of COS4 in port 0 ETS command arbiter. */
#define PBF_REG_COS4_WEIGHT_P0					 0x15c2b8
/* [RW 31] The weight of COS5 in port 0 ETS command arbiter. */
#define PBF_REG_COS5_WEIGHT_P0					 0x15c2bc
/* [R 11] Current credit for the LB queue in the tx port buffers in 16 byte
 * lines. */
#define PBF_REG_CREDIT_LB_Q					 0x140338
/* [R 11] Current credit for queue 0 in the tx port buffers in 16 byte
 * lines. */
#define PBF_REG_CREDIT_Q0					 0x14033c
/* [R 11] Current credit for queue 1 in the tx port buffers in 16 byte
 * lines. */
#define PBF_REG_CREDIT_Q1					 0x140340
/* [RW 1] Disable processing further tasks from port 0 (after ending the
   current task in process). */
#define PBF_REG_DISABLE_NEW_TASK_PROC_P0			 0x14005c
/* [RW 1] Disable processing further tasks from port 1 (after ending the
   current task in process). */
#define PBF_REG_DISABLE_NEW_TASK_PROC_P1			 0x140060
/* [RW 1] Disable processing further tasks from port 4 (after ending the
   current task in process). */
#define PBF_REG_DISABLE_NEW_TASK_PROC_P4			 0x14006c
#define PBF_REG_DISABLE_PF					 0x1402e8
/* [RW 18] For port 0: For each client that is subject to WFQ (the
 * corresponding bit is 1); indicates to which of the credit registers this
 * client is mapped. For clients which are not credit blocked; their mapping
 * is dont care. */
#define PBF_REG_ETS_ARB_CLIENT_CREDIT_MAP_P0			 0x15c288
/* [RW 9] For port 1: For each client that is subject to WFQ (the
 * corresponding bit is 1); indicates to which of the credit registers this
 * client is mapped. For clients which are not credit blocked; their mapping
 * is dont care. */
#define PBF_REG_ETS_ARB_CLIENT_CREDIT_MAP_P1			 0x15c28c
/* [RW 6] For port 0: Bit per client to indicate if the client competes in
 * the strict priority arbiter directly (corresponding bit = 1); or first
 * goes to the RR arbiter (corresponding bit = 0); and then competes in the
 * lowest priority in the strict-priority arbiter. */
#define PBF_REG_ETS_ARB_CLIENT_IS_STRICT_P0			 0x15c278
/* [RW 3] For port 1: Bit per client to indicate if the client competes in
 * the strict priority arbiter directly (corresponding bit = 1); or first
 * goes to the RR arbiter (corresponding bit = 0); and then competes in the
 * lowest priority in the strict-priority arbiter. */
#define PBF_REG_ETS_ARB_CLIENT_IS_STRICT_P1			 0x15c27c
/* [RW 6] For port 0: Bit per client to indicate if the client is subject to
 * WFQ credit blocking (corresponding bit = 1). */
#define PBF_REG_ETS_ARB_CLIENT_IS_SUBJECT2WFQ_P0		 0x15c280
/* [RW 3] For port 0: Bit per client to indicate if the client is subject to
 * WFQ credit blocking (corresponding bit = 1). */
#define PBF_REG_ETS_ARB_CLIENT_IS_SUBJECT2WFQ_P1		 0x15c284
/* [RW 16] For port 0: The number of strict priority arbitration slots
 * between 2 RR arbitration slots. A value of 0 means no strict priority
 * cycles; i.e. the strict-priority w/ anti-starvation arbiter is a RR
 * arbiter. */
#define PBF_REG_ETS_ARB_NUM_STRICT_ARB_SLOTS_P0			 0x15c2a0
/* [RW 16] For port 1: The number of strict priority arbitration slots
 * between 2 RR arbitration slots. A value of 0 means no strict priority
 * cycles; i.e. the strict-priority w/ anti-starvation arbiter is a RR
 * arbiter. */
#define PBF_REG_ETS_ARB_NUM_STRICT_ARB_SLOTS_P1			 0x15c2a4
/* [RW 18] For port 0: Indicates which client is connected to each priority
 * in the strict-priority arbiter. Priority 0 is the highest priority, and
 * priority 5 is the lowest; to which the RR output is connected to (this is
 * not configurable). */
#define PBF_REG_ETS_ARB_PRIORITY_CLIENT_P0			 0x15c270
/* [RW 9] For port 1: Indicates which client is connected to each priority
 * in the strict-priority arbiter. Priority 0 is the highest priority, and
 * priority 5 is the lowest; to which the RR output is connected to (this is
 * not configurable). */
#define PBF_REG_ETS_ARB_PRIORITY_CLIENT_P1			 0x15c274
/* [RW 1] Indicates that ETS is performed between the COSes in the command
 * arbiter. If reset strict priority w/ anti-starvation will be performed
 * w/o WFQ. */
#define PBF_REG_ETS_ENABLED					 0x15c050
/* [RW 6] Bit-map indicating which L2 hdrs may appear after the basic
 * Ethernet header. */
#define PBF_REG_HDRS_AFTER_BASIC				 0x15c0a8
/* [RW 6] Bit-map indicating which L2 hdrs may appear after L2 tag 0 */
#define PBF_REG_HDRS_AFTER_TAG_0				 0x15c0b8
/* [R 1] Removed for E3 B0 - Indicates which COS is conncted to the highest
 * priority in the command arbiter. */
#define PBF_REG_HIGH_PRIORITY_COS_NUM				 0x15c04c
#define PBF_REG_IF_ENABLE_REG					 0x140044
/* [RW 1] Init bit. When set the initial credits are copied to the credit
   registers (except the port credits). Should be set and then reset after
   the configuration of the block has ended. */
#define PBF_REG_INIT						 0x140000
/* [RW 11] Initial credit for the LB queue in the tx port buffers in 16 byte
 * lines. */
#define PBF_REG_INIT_CRD_LB_Q					 0x15c248
/* [RW 11] Initial credit for queue 0 in the tx port buffers in 16 byte
 * lines. */
#define PBF_REG_INIT_CRD_Q0					 0x15c230
/* [RW 11] Initial credit for queue 1 in the tx port buffers in 16 byte
 * lines. */
#define PBF_REG_INIT_CRD_Q1					 0x15c234
/* [RW 1] Init bit for port 0. When set the initial credit of port 0 is
   copied to the credit register. Should be set and then reset after the
   configuration of the port has ended. */
#define PBF_REG_INIT_P0 					 0x140004
/* [RW 1] Init bit for port 1. When set the initial credit of port 1 is
   copied to the credit register. Should be set and then reset after the
   configuration of the port has ended. */
#define PBF_REG_INIT_P1 					 0x140008
/* [RW 1] Init bit for port 4. When set the initial credit of port 4 is
   copied to the credit register. Should be set and then reset after the
   configuration of the port has ended. */
#define PBF_REG_INIT_P4 					 0x14000c
/* [R 32] Cyclic counter for the amount credits in 16 bytes lines added for
 * the LB queue. Reset upon init. */
#define PBF_REG_INTERNAL_CRD_FREED_CNT_LB_Q			 0x140354
/* [R 32] Cyclic counter for the amount credits in 16 bytes lines added for
 * queue 0. Reset upon init. */
#define PBF_REG_INTERNAL_CRD_FREED_CNT_Q0			 0x140358
/* [R 32] Cyclic counter for the amount credits in 16 bytes lines added for
 * queue 1. Reset upon init. */
#define PBF_REG_INTERNAL_CRD_FREED_CNT_Q1			 0x14035c
/* [RW 1] Enable for mac interface 0. */
#define PBF_REG_MAC_IF0_ENABLE					 0x140030
/* [RW 1] Enable for mac interface 1. */
#define PBF_REG_MAC_IF1_ENABLE					 0x140034
/* [RW 1] Enable for the loopback interface. */
#define PBF_REG_MAC_LB_ENABLE					 0x140040
/* [RW 6] Bit-map indicating which headers must appear in the packet */
#define PBF_REG_MUST_HAVE_HDRS					 0x15c0c4
/* [RW 16] The number of strict priority arbitration slots between 2 RR
 * arbitration slots. A value of 0 means no strict priority cycles; i.e. the
 * strict-priority w/ anti-starvation arbiter is a RR arbiter. */
#define PBF_REG_NUM_STRICT_ARB_SLOTS				 0x15c064
/* [RW 10] Port 0 threshold used by arbiter in 16 byte lines used when pause
   not suppoterd. */
#define PBF_REG_P0_ARB_THRSH					 0x1400e4
/* [R 11] Current credit for port 0 in the tx port buffers in 16 byte lines. */
#define PBF_REG_P0_CREDIT					 0x140200
/* [RW 11] Initial credit for port 0 in the tx port buffers in 16 byte
   lines. */
#define PBF_REG_P0_INIT_CRD					 0x1400d0
/* [R 32] Cyclic counter for the amount credits in 16 bytes lines added for
 * port 0. Reset upon init. */
#define PBF_REG_P0_INTERNAL_CRD_FREED_CNT			 0x140308
/* [R 1] Removed for E3 B0 - Indication that pause is enabled for port 0. */
#define PBF_REG_P0_PAUSE_ENABLE					 0x140014
/* [R 8] Removed for E3 B0 - Number of tasks in port 0 task queue. */
#define PBF_REG_P0_TASK_CNT					 0x140204
/* [R 32] Removed for E3 B0 - Cyclic counter for number of 8 byte lines
 * freed from the task queue of port 0. Reset upon init. */
#define PBF_REG_P0_TQ_LINES_FREED_CNT				 0x1402f0
/* [R 12] Number of 8 bytes lines occupied in the task queue of port 0. */
#define PBF_REG_P0_TQ_OCCUPANCY					 0x1402fc
/* [R 11] Removed for E3 B0 - Current credit for port 1 in the tx port
 * buffers in 16 byte lines. */
#define PBF_REG_P1_CREDIT					 0x140208
/* [R 11] Removed for E3 B0 - Initial credit for port 0 in the tx port
 * buffers in 16 byte lines. */
#define PBF_REG_P1_INIT_CRD					 0x1400d4
/* [R 32] Cyclic counter for the amount credits in 16 bytes lines added for
 * port 1. Reset upon init. */
#define PBF_REG_P1_INTERNAL_CRD_FREED_CNT			 0x14030c
/* [R 8] Removed for E3 B0 - Number of tasks in port 1 task queue. */
#define PBF_REG_P1_TASK_CNT					 0x14020c
/* [R 32] Removed for E3 B0 - Cyclic counter for number of 8 byte lines
 * freed from the task queue of port 1. Reset upon init. */
#define PBF_REG_P1_TQ_LINES_FREED_CNT				 0x1402f4
/* [R 12] Number of 8 bytes lines occupied in the task queue of port 1. */
#define PBF_REG_P1_TQ_OCCUPANCY					 0x140300
/* [R 11] Current credit for port 4 in the tx port buffers in 16 byte lines. */
#define PBF_REG_P4_CREDIT					 0x140210
/* [RW 11] Initial credit for port 4 in the tx port buffers in 16 byte
   lines. */
#define PBF_REG_P4_INIT_CRD					 0x1400e0
/* [R 32] Cyclic counter for the amount credits in 16 bytes lines added for
 * port 4. Reset upon init. */
#define PBF_REG_P4_INTERNAL_CRD_FREED_CNT			 0x140310
/* [R 8] Removed for E3 B0 - Number of tasks in port 4 task queue. */
#define PBF_REG_P4_TASK_CNT					 0x140214
/* [R 32] Removed for E3 B0 - Cyclic counter for number of 8 byte lines
 * freed from the task queue of port 4. Reset upon init. */
#define PBF_REG_P4_TQ_LINES_FREED_CNT				 0x1402f8
/* [R 12] Number of 8 bytes lines occupied in the task queue of port 4. */
#define PBF_REG_P4_TQ_OCCUPANCY					 0x140304
/* [RW 5] Interrupt mask register #0 read/write */
#define PBF_REG_PBF_INT_MASK					 0x1401d4
/* [R 5] Interrupt register #0 read */
#define PBF_REG_PBF_INT_STS					 0x1401c8
/* [RW 20] Parity mask register #0 read/write */
#define PBF_REG_PBF_PRTY_MASK					 0x1401e4
/* [RC 20] Parity register #0 read clear */
#define PBF_REG_PBF_PRTY_STS_CLR				 0x1401dc
/* [RW 16] The Ethernet type value for L2 tag 0 */
#define PBF_REG_TAG_ETHERTYPE_0					 0x15c090
/* [RW 4] The length of the info field for L2 tag 0. The length is between
 * 2B and 14B; in 2B granularity */
#define PBF_REG_TAG_LEN_0					 0x15c09c
/* [R 32] Cyclic counter for number of 8 byte lines freed from the LB task
 * queue. Reset upon init. */
#define PBF_REG_TQ_LINES_FREED_CNT_LB_Q				 0x14038c
/* [R 32] Cyclic counter for number of 8 byte lines freed from the task
 * queue 0. Reset upon init. */
#define PBF_REG_TQ_LINES_FREED_CNT_Q0				 0x140390
/* [R 32] Cyclic counter for number of 8 byte lines freed from task queue 1.
 * Reset upon init. */
#define PBF_REG_TQ_LINES_FREED_CNT_Q1				 0x140394
/* [R 13] Number of 8 bytes lines occupied in the task queue of the LB
 * queue. */
#define PBF_REG_TQ_OCCUPANCY_LB_Q				 0x1403a8
/* [R 13] Number of 8 bytes lines occupied in the task queue of queue 0. */
#define PBF_REG_TQ_OCCUPANCY_Q0					 0x1403ac
/* [R 13] Number of 8 bytes lines occupied in the task queue of queue 1. */
#define PBF_REG_TQ_OCCUPANCY_Q1					 0x1403b0
#define PB_REG_CONTROL						 0
/* [RW 2] Interrupt mask register #0 read/write */
#define PB_REG_PB_INT_MASK					 0x28
/* [R 2] Interrupt register #0 read */
#define PB_REG_PB_INT_STS					 0x1c
/* [RW 4] Parity mask register #0 read/write */
#define PB_REG_PB_PRTY_MASK					 0x38
/* [R 4] Parity register #0 read */
#define PB_REG_PB_PRTY_STS					 0x2c
/* [RC 4] Parity register #0 read clear */
#define PB_REG_PB_PRTY_STS_CLR					 0x30
#define PGLUE_B_PGLUE_B_INT_STS_REG_ADDRESS_ERROR		 (0x1<<0)
#define PGLUE_B_PGLUE_B_INT_STS_REG_CSSNOOP_FIFO_OVERFLOW	 (0x1<<8)
#define PGLUE_B_PGLUE_B_INT_STS_REG_INCORRECT_RCV_BEHAVIOR	 (0x1<<1)
#define PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_ERROR_ATTN		 (0x1<<6)
#define PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_IN_TWO_RCBS_ATTN	 (0x1<<7)
#define PGLUE_B_PGLUE_B_INT_STS_REG_VF_GRC_SPACE_VIOLATION_ATTN  (0x1<<4)
#define PGLUE_B_PGLUE_B_INT_STS_REG_VF_LENGTH_VIOLATION_ATTN	 (0x1<<3)
#define PGLUE_B_PGLUE_B_INT_STS_REG_VF_MSIX_BAR_VIOLATION_ATTN	 (0x1<<5)
#define PGLUE_B_PGLUE_B_INT_STS_REG_WAS_ERROR_ATTN		 (0x1<<2)
/* [R 8] Config space A attention dirty bits. Each bit indicates that the
 * corresponding PF generates config space A attention. Set by PXP. Reset by
 * MCP writing 1 to icfg_space_a_request_clr. Note: register contains bits
 * from both paths. */
#define PGLUE_B_REG_CFG_SPACE_A_REQUEST			 0x9010
/* [R 8] Config space B attention dirty bits. Each bit indicates that the
 * corresponding PF generates config space B attention. Set by PXP. Reset by
 * MCP writing 1 to icfg_space_b_request_clr. Note: register contains bits
 * from both paths. */
#define PGLUE_B_REG_CFG_SPACE_B_REQUEST			 0x9014
/* [RW 1] Type A PF enable inbound interrupt table for CSDM. 0 - disable; 1
 * - enable. */
#define PGLUE_B_REG_CSDM_INB_INT_A_PF_ENABLE			 0x9194
/* [RW 18] Type B VF inbound interrupt table for CSDM: bits[17:9]-mask;
 * its[8:0]-address. Bits [1:0] must be zero (DW resolution address). */
#define PGLUE_B_REG_CSDM_INB_INT_B_VF				 0x916c
/* [RW 1] Type B VF enable inbound interrupt table for CSDM. 0 - disable; 1
 * - enable. */
#define PGLUE_B_REG_CSDM_INB_INT_B_VF_ENABLE			 0x919c
/* [RW 16] Start offset of CSDM zone A (queue zone) in the internal RAM */
#define PGLUE_B_REG_CSDM_START_OFFSET_A			 0x9100
/* [RW 16] Start offset of CSDM zone B (legacy zone) in the internal RAM */
#define PGLUE_B_REG_CSDM_START_OFFSET_B			 0x9108
/* [RW 5] VF Shift of CSDM zone B (legacy zone) in the internal RAM */
#define PGLUE_B_REG_CSDM_VF_SHIFT_B				 0x9110
/* [RW 1] 0 - Zone A size is 136x32B; 1 - Zone A size is 152x32B. */
#define PGLUE_B_REG_CSDM_ZONE_A_SIZE_PF			 0x91ac
/* [R 8] FLR request attention dirty bits for PFs 0 to 7. Each bit indicates
 * that the FLR register of the corresponding PF was set. Set by PXP. Reset
 * by MCP writing 1 to flr_request_pf_7_0_clr. Note: register contains bits
 * from both paths. */
#define PGLUE_B_REG_FLR_REQUEST_PF_7_0				 0x9028
/* [W 8] FLR request attention dirty bits clear for PFs 0 to 7. MCP writes 1
 * to a bit in this register in order to clear the corresponding bit in
 * flr_request_pf_7_0 register. Note: register contains bits from both
 * paths. */
#define PGLUE_B_REG_FLR_REQUEST_PF_7_0_CLR			 0x9418
/* [R 32] FLR request attention dirty bits for VFs 96 to 127. Each bit
 * indicates that the FLR register of the corresponding VF was set. Set by
 * PXP. Reset by MCP writing 1 to flr_request_vf_127_96_clr. */
#define PGLUE_B_REG_FLR_REQUEST_VF_127_96			 0x9024
/* [R 32] FLR request attention dirty bits for VFs 0 to 31. Each bit
 * indicates that the FLR register of the corresponding VF was set. Set by
 * PXP. Reset by MCP writing 1 to flr_request_vf_31_0_clr. */
#define PGLUE_B_REG_FLR_REQUEST_VF_31_0			 0x9018
/* [R 32] FLR request attention dirty bits for VFs 32 to 63. Each bit
 * indicates that the FLR register of the corresponding VF was set. Set by
 * PXP. Reset by MCP writing 1 to flr_request_vf_63_32_clr. */
#define PGLUE_B_REG_FLR_REQUEST_VF_63_32			 0x901c
/* [R 32] FLR request attention dirty bits for VFs 64 to 95. Each bit
 * indicates that the FLR register of the corresponding VF was set. Set by
 * PXP. Reset by MCP writing 1 to flr_request_vf_95_64_clr. */
#define PGLUE_B_REG_FLR_REQUEST_VF_95_64			 0x9020
/* [R 8] Each bit indicates an incorrect behavior in user RX interface. Bit
 * 0 - Target memory read arrived with a correctable error. Bit 1 - Target
 * memory read arrived with an uncorrectable error. Bit 2 - Configuration RW
 * arrived with a correctable error. Bit 3 - Configuration RW arrived with
 * an uncorrectable error. Bit 4 - Completion with Configuration Request
 * Retry Status. Bit 5 - Expansion ROM access received with a write request.
 * Bit 6 - Completion with pcie_rx_err of 0000; CMPL_STATUS of non-zero; and
 * pcie_rx_last not asserted. Bit 7 - Completion with pcie_rx_err of 1010;
 * and pcie_rx_last not asserted. */
#define PGLUE_B_REG_INCORRECT_RCV_DETAILS			 0x9068
#define PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER		 0x942c
#define PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ		 0x9430
#define PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_WRITE		 0x9434
#define PGLUE_B_REG_INTERNAL_VFID_ENABLE			 0x9438
/* [R 9] Interrupt register #0 read */
#define PGLUE_B_REG_PGLUE_B_INT_STS				 0x9298
/* [RC 9] Interrupt register #0 read clear */
#define PGLUE_B_REG_PGLUE_B_INT_STS_CLR			 0x929c
/* [RW 2] Parity mask register #0 read/write */
#define PGLUE_B_REG_PGLUE_B_PRTY_MASK				 0x92b4
/* [R 2] Parity register #0 read */
#define PGLUE_B_REG_PGLUE_B_PRTY_STS				 0x92a8
/* [RC 2] Parity register #0 read clear */
#define PGLUE_B_REG_PGLUE_B_PRTY_STS_CLR			 0x92ac
/* [R 13] Details of first request received with error. [2:0] - PFID. [3] -
 * VF_VALID. [9:4] - VFID. [11:10] - Error Code - 0 - Indicates Completion
 * Timeout of a User Tx non-posted request. 1 - unsupported request. 2 -
 * completer abort. 3 - Illegal value for this field. [12] valid - indicates
 * if there was a completion error since the last time this register was
 * cleared. */
#define PGLUE_B_REG_RX_ERR_DETAILS				 0x9080
/* [R 18] Details of first ATS Translation Completion request received with
 * error. [2:0] - PFID. [3] - VF_VALID. [9:4] - VFID. [11:10] - Error Code -
 * 0 - Indicates Completion Timeout of a User Tx non-posted request. 1 -
 * unsupported request. 2 - completer abort. 3 - Illegal value for this
 * field. [16:12] - ATC OTB EntryID. [17] valid - indicates if there was a
 * completion error since the last time this register was cleared. */
#define PGLUE_B_REG_RX_TCPL_ERR_DETAILS			 0x9084
/* [W 8] Debug only - Shadow BME bits clear for PFs 0 to 7. MCP writes 1 to
 * a bit in this register in order to clear the corresponding bit in
 * shadow_bme_pf_7_0 register. MCP should never use this unless a
 * work-around is needed. Note: register contains bits from both paths. */
#define PGLUE_B_REG_SHADOW_BME_PF_7_0_CLR			 0x9458
/* [R 8] SR IOV disabled attention dirty bits. Each bit indicates that the
 * VF enable register of the corresponding PF is written to 0 and was
 * previously 1. Set by PXP. Reset by MCP writing 1 to
 * sr_iov_disabled_request_clr. Note: register contains bits from both
 * paths. */
#define PGLUE_B_REG_SR_IOV_DISABLED_REQUEST			 0x9030
/* [R 32] Indicates the status of tags 32-63. 0 - tags is used - read
 * completion did not return yet. 1 - tag is unused. Same functionality as
 * pxp2_registers_pgl_exp_rom_data2 for tags 0-31. */
#define PGLUE_B_REG_TAGS_63_32					 0x9244
/* [RW 1] Type A PF enable inbound interrupt table for TSDM. 0 - disable; 1
 * - enable. */
#define PGLUE_B_REG_TSDM_INB_INT_A_PF_ENABLE			 0x9170
/* [RW 16] Start offset of TSDM zone A (queue zone) in the internal RAM */
#define PGLUE_B_REG_TSDM_START_OFFSET_A			 0x90c4
/* [RW 16] Start offset of TSDM zone B (legacy zone) in the internal RAM */
#define PGLUE_B_REG_TSDM_START_OFFSET_B			 0x90cc
/* [RW 5] VF Shift of TSDM zone B (legacy zone) in the internal RAM */
#define PGLUE_B_REG_TSDM_VF_SHIFT_B				 0x90d4
/* [RW 1] 0 - Zone A size is 136x32B; 1 - Zone A size is 152x32B. */
#define PGLUE_B_REG_TSDM_ZONE_A_SIZE_PF			 0x91a0
/* [R 32] Address [31:0] of first read request not submitted due to error */
#define PGLUE_B_REG_TX_ERR_RD_ADD_31_0				 0x9098
/* [R 32] Address [63:32] of first read request not submitted due to error */
#define PGLUE_B_REG_TX_ERR_RD_ADD_63_32			 0x909c
/* [R 31] Details of first read request not submitted due to error. [4:0]
 * VQID. [5] TREQ. 1 - Indicates the request is a Translation Request.
 * [20:8] - Length in bytes. [23:21] - PFID. [24] - VF_VALID. [30:25] -
 * VFID. */
#define PGLUE_B_REG_TX_ERR_RD_DETAILS				 0x90a0
/* [R 26] Details of first read request not submitted due to error. [15:0]
 * Request ID. [19:16] client ID. [20] - last SR. [24:21] - Error type -
 * [21] - Indicates was_error was set; [22] - Indicates BME was cleared;
 * [23] - Indicates FID_enable was cleared; [24] - Indicates VF with parent
 * PF FLR_request or IOV_disable_request dirty bit is set. [25] valid -
 * indicates if there was a request not submitted due to error since the
 * last time this register was cleared. */
#define PGLUE_B_REG_TX_ERR_RD_DETAILS2				 0x90a4
/* [R 32] Address [31:0] of first write request not submitted due to error */
#define PGLUE_B_REG_TX_ERR_WR_ADD_31_0				 0x9088
/* [R 32] Address [63:32] of first write request not submitted due to error */
#define PGLUE_B_REG_TX_ERR_WR_ADD_63_32			 0x908c
/* [R 31] Details of first write request not submitted due to error. [4:0]
 * VQID. [20:8] - Length in bytes. [23:21] - PFID. [24] - VF_VALID. [30:25]
 * - VFID. */
#define PGLUE_B_REG_TX_ERR_WR_DETAILS				 0x9090
/* [R 26] Details of first write request not submitted due to error. [15:0]
 * Request ID. [19:16] client ID. [20] - last SR. [24:21] - Error type -
 * [21] - Indicates was_error was set; [22] - Indicates BME was cleared;
 * [23] - Indicates FID_enable was cleared; [24] - Indicates VF with parent
 * PF FLR_request or IOV_disable_request dirty bit is set. [25] valid -
 * indicates if there was a request not submitted due to error since the
 * last time this register was cleared. */
#define PGLUE_B_REG_TX_ERR_WR_DETAILS2				 0x9094
/* [RW 10] Type A PF/VF inbound interrupt table for USDM: bits[9:5]-mask;
 * its[4:0]-address relative to start_offset_a. Bits [1:0] can have any
 * value (Byte resolution address). */
#define PGLUE_B_REG_USDM_INB_INT_A_0				 0x9128
#define PGLUE_B_REG_USDM_INB_INT_A_1				 0x912c
#define PGLUE_B_REG_USDM_INB_INT_A_2				 0x9130
#define PGLUE_B_REG_USDM_INB_INT_A_3				 0x9134
#define PGLUE_B_REG_USDM_INB_INT_A_4				 0x9138
#define PGLUE_B_REG_USDM_INB_INT_A_5				 0x913c
#define PGLUE_B_REG_USDM_INB_INT_A_6				 0x9140
/* [RW 1] Type A PF enable inbound interrupt table for USDM. 0 - disable; 1
 * - enable. */
#define PGLUE_B_REG_USDM_INB_INT_A_PF_ENABLE			 0x917c
/* [RW 1] Type A VF enable inbound interrupt table for USDM. 0 - disable; 1
 * - enable. */
#define PGLUE_B_REG_USDM_INB_INT_A_VF_ENABLE			 0x9180
/* [RW 1] Type B VF enable inbound interrupt table for USDM. 0 - disable; 1
 * - enable. */
#define PGLUE_B_REG_USDM_INB_INT_B_VF_ENABLE			 0x9184
/* [RW 16] Start offset of USDM zone A (queue zone) in the internal RAM */
#define PGLUE_B_REG_USDM_START_OFFSET_A			 0x90d8
/* [RW 16] Start offset of USDM zone B (legacy zone) in the internal RAM */
#define PGLUE_B_REG_USDM_START_OFFSET_B			 0x90e0
/* [RW 5] VF Shift of USDM zone B (legacy zone) in the internal RAM */
#define PGLUE_B_REG_USDM_VF_SHIFT_B				 0x90e8
/* [RW 1] 0 - Zone A size is 136x32B; 1 - Zone A size is 152x32B. */
#define PGLUE_B_REG_USDM_ZONE_A_SIZE_PF			 0x91a4
/* [R 26] Details of first target VF request accessing VF GRC space that
 * failed permission check. [14:0] Address. [15] w_nr: 0 - Read; 1 - Write.
 * [21:16] VFID. [24:22] - PFID. [25] valid - indicates if there was a
 * request accessing VF GRC space that failed permission check since the
 * last time this register was cleared. Permission checks are: function
 * permission; R/W permission; address range permission. */
#define PGLUE_B_REG_VF_GRC_SPACE_VIOLATION_DETAILS		 0x9234
/* [R 31] Details of first target VF request with length violation (too many
 * DWs) accessing BAR0. [12:0] Address in DWs (bits [14:2] of byte address).
 * [14:13] BAR. [20:15] VFID. [23:21] - PFID. [29:24] - Length in DWs. [30]
 * valid - indicates if there was a request with length violation since the
 * last time this register was cleared. Length violations: length of more
 * than 2DWs; length of 2DWs and address not QW aligned; window is GRC and
 * length is more than 1 DW. */
#define PGLUE_B_REG_VF_LENGTH_VIOLATION_DETAILS		 0x9230
/* [R 8] Was_error indication dirty bits for PFs 0 to 7. Each bit indicates
 * that there was a completion with uncorrectable error for the
 * corresponding PF. Set by PXP. Reset by MCP writing 1 to
 * was_error_pf_7_0_clr. */
#define PGLUE_B_REG_WAS_ERROR_PF_7_0				 0x907c
/* [W 8] Was_error indication dirty bits clear for PFs 0 to 7. MCP writes 1
 * to a bit in this register in order to clear the corresponding bit in
 * flr_request_pf_7_0 register. */
#define PGLUE_B_REG_WAS_ERROR_PF_7_0_CLR			 0x9470
/* [R 32] Was_error indication dirty bits for VFs 96 to 127. Each bit
 * indicates that there was a completion with uncorrectable error for the
 * corresponding VF. Set by PXP. Reset by MCP writing 1 to
 * was_error_vf_127_96_clr. */
#define PGLUE_B_REG_WAS_ERROR_VF_127_96			 0x9078
/* [W 32] Was_error indication dirty bits clear for VFs 96 to 127. MCP
 * writes 1 to a bit in this register in order to clear the corresponding
 * bit in was_error_vf_127_96 register. */
#define PGLUE_B_REG_WAS_ERROR_VF_127_96_CLR			 0x9474
/* [R 32] Was_error indication dirty bits for VFs 0 to 31. Each bit
 * indicates that there was a completion with uncorrectable error for the
 * corresponding VF. Set by PXP. Reset by MCP writing 1 to
 * was_error_vf_31_0_clr. */
#define PGLUE_B_REG_WAS_ERROR_VF_31_0				 0x906c
/* [W 32] Was_error indication dirty bits clear for VFs 0 to 31. MCP writes
 * 1 to a bit in this register in order to clear the corresponding bit in
 * was_error_vf_31_0 register. */
#define PGLUE_B_REG_WAS_ERROR_VF_31_0_CLR			 0x9478
/* [R 32] Was_error indication dirty bits for VFs 32 to 63. Each bit
 * indicates that there was a completion with uncorrectable error for the
 * corresponding VF. Set by PXP. Reset by MCP writing 1 to
 * was_error_vf_63_32_clr. */
#define PGLUE_B_REG_WAS_ERROR_VF_63_32				 0x9070
/* [W 32] Was_error indication dirty bits clear for VFs 32 to 63. MCP writes
 * 1 to a bit in this register in order to clear the corresponding bit in
 * was_error_vf_63_32 register. */
#define PGLUE_B_REG_WAS_ERROR_VF_63_32_CLR			 0x947c
/* [R 32] Was_error indication dirty bits for VFs 64 to 95. Each bit
 * indicates that there was a completion with uncorrectable error for the
 * corresponding VF. Set by PXP. Reset by MCP writing 1 to
 * was_error_vf_95_64_clr. */
#define PGLUE_B_REG_WAS_ERROR_VF_95_64				 0x9074
/* [W 32] Was_error indication dirty bits clear for VFs 64 to 95. MCP writes
 * 1 to a bit in this register in order to clear the corresponding bit in
 * was_error_vf_95_64 register. */
#define PGLUE_B_REG_WAS_ERROR_VF_95_64_CLR			 0x9480
/* [RW 1] Type A PF enable inbound interrupt table for XSDM. 0 - disable; 1
 * - enable. */
#define PGLUE_B_REG_XSDM_INB_INT_A_PF_ENABLE			 0x9188
/* [RW 16] Start offset of XSDM zone A (queue zone) in the internal RAM */
#define PGLUE_B_REG_XSDM_START_OFFSET_A			 0x90ec
/* [RW 16] Start offset of XSDM zone B (legacy zone) in the internal RAM */
#define PGLUE_B_REG_XSDM_START_OFFSET_B			 0x90f4
/* [RW 5] VF Shift of XSDM zone B (legacy zone) in the internal RAM */
#define PGLUE_B_REG_XSDM_VF_SHIFT_B				 0x90fc
/* [RW 1] 0 - Zone A size is 136x32B; 1 - Zone A size is 152x32B. */
#define PGLUE_B_REG_XSDM_ZONE_A_SIZE_PF			 0x91a8
#define PRS_REG_A_PRSU_20					 0x40134
/* [R 8] debug only: CFC load request current credit. Transaction based. */
#define PRS_REG_CFC_LD_CURRENT_CREDIT				 0x40164
/* [R 8] debug only: CFC search request current credit. Transaction based. */
#define PRS_REG_CFC_SEARCH_CURRENT_CREDIT			 0x40168
/* [RW 6] The initial credit for the search message to the CFC interface.
   Credit is transaction based. */
#define PRS_REG_CFC_SEARCH_INITIAL_CREDIT			 0x4011c
/* [RW 24] CID for port 0 if no match */
#define PRS_REG_CID_PORT_0					 0x400fc
/* [RW 32] The CM header for flush message where 'load existed' bit in CFC
   load response is reset and packet type is 0. Used in packet start message
   to TCM. */
#define PRS_REG_CM_HDR_FLUSH_LOAD_TYPE_0			 0x400dc
#define PRS_REG_CM_HDR_FLUSH_LOAD_TYPE_1			 0x400e0
#define PRS_REG_CM_HDR_FLUSH_LOAD_TYPE_2			 0x400e4
#define PRS_REG_CM_HDR_FLUSH_LOAD_TYPE_3			 0x400e8
#define PRS_REG_CM_HDR_FLUSH_LOAD_TYPE_4			 0x400ec
#define PRS_REG_CM_HDR_FLUSH_LOAD_TYPE_5			 0x400f0
/* [RW 32] The CM header for flush message where 'load existed' bit in CFC
   load response is set and packet type is 0. Used in packet start message
   to TCM. */
#define PRS_REG_CM_HDR_FLUSH_NO_LOAD_TYPE_0			 0x400bc
#define PRS_REG_CM_HDR_FLUSH_NO_LOAD_TYPE_1			 0x400c0
#define PRS_REG_CM_HDR_FLUSH_NO_LOAD_TYPE_2			 0x400c4
#define PRS_REG_CM_HDR_FLUSH_NO_LOAD_TYPE_3			 0x400c8
#define PRS_REG_CM_HDR_FLUSH_NO_LOAD_TYPE_4			 0x400cc
#define PRS_REG_CM_HDR_FLUSH_NO_LOAD_TYPE_5			 0x400d0
/* [RW 32] The CM header for a match and packet type 1 for loopback port.
   Used in packet start message to TCM. */
#define PRS_REG_CM_HDR_LOOPBACK_TYPE_1				 0x4009c
#define PRS_REG_CM_HDR_LOOPBACK_TYPE_2				 0x400a0
#define PRS_REG_CM_HDR_LOOPBACK_TYPE_3				 0x400a4
#define PRS_REG_CM_HDR_LOOPBACK_TYPE_4				 0x400a8
/* [RW 32] The CM header for a match and packet type 0. Used in packet start
   message to TCM. */
#define PRS_REG_CM_HDR_TYPE_0					 0x40078
#define PRS_REG_CM_HDR_TYPE_1					 0x4007c
#define PRS_REG_CM_HDR_TYPE_2					 0x40080
#define PRS_REG_CM_HDR_TYPE_3					 0x40084
#define PRS_REG_CM_HDR_TYPE_4					 0x40088
/* [RW 32] The CM header in case there was not a match on the connection */
#define PRS_REG_CM_NO_MATCH_HDR 				 0x400b8
/* [RW 1] Indicates if in e1hov mode. 0=non-e1hov mode; 1=e1hov mode. */
#define PRS_REG_E1HOV_MODE					 0x401c8
/* [RW 8] The 8-bit event ID for a match and packet type 1. Used in packet
   start message to TCM. */
#define PRS_REG_EVENT_ID_1					 0x40054
#define PRS_REG_EVENT_ID_2					 0x40058
#define PRS_REG_EVENT_ID_3					 0x4005c
/* [RW 16] The Ethernet type value for FCoE */
#define PRS_REG_FCOE_TYPE					 0x401d0
/* [RW 8] Context region for flush packet with packet type 0. Used in CFC
   load request message. */
#define PRS_REG_FLUSH_REGIONS_TYPE_0				 0x40004
#define PRS_REG_FLUSH_REGIONS_TYPE_1				 0x40008
#define PRS_REG_FLUSH_REGIONS_TYPE_2				 0x4000c
#define PRS_REG_FLUSH_REGIONS_TYPE_3				 0x40010
#define PRS_REG_FLUSH_REGIONS_TYPE_4				 0x40014
#define PRS_REG_FLUSH_REGIONS_TYPE_5				 0x40018
#define PRS_REG_FLUSH_REGIONS_TYPE_6				 0x4001c
#define PRS_REG_FLUSH_REGIONS_TYPE_7				 0x40020
/* [RW 6] Bit-map indicating which L2 hdrs may appear after the basic
 * Ethernet header. */
#define PRS_REG_HDRS_AFTER_BASIC				 0x40238
/* [RW 6] Bit-map indicating which L2 hdrs may appear after the basic
 * Ethernet header for port 0 packets. */
#define PRS_REG_HDRS_AFTER_BASIC_PORT_0				 0x40270
#define PRS_REG_HDRS_AFTER_BASIC_PORT_1				 0x40290
/* [R 6] Bit-map indicating which L2 hdrs may appear after L2 tag 0 */
#define PRS_REG_HDRS_AFTER_TAG_0				 0x40248
/* [RW 6] Bit-map indicating which L2 hdrs may appear after L2 tag 0 for
 * port 0 packets */
#define PRS_REG_HDRS_AFTER_TAG_0_PORT_0				 0x40280
#define PRS_REG_HDRS_AFTER_TAG_0_PORT_1				 0x402a0
/* [RW 4] The increment value to send in the CFC load request message */
#define PRS_REG_INC_VALUE					 0x40048
/* [RW 6] Bit-map indicating which headers must appear in the packet */
#define PRS_REG_MUST_HAVE_HDRS					 0x40254
/* [RW 6] Bit-map indicating which headers must appear in the packet for
 * port 0 packets */
#define PRS_REG_MUST_HAVE_HDRS_PORT_0				 0x4028c
#define PRS_REG_MUST_HAVE_HDRS_PORT_1				 0x402ac
#define PRS_REG_NIC_MODE					 0x40138
/* [RW 8] The 8-bit event ID for cases where there is no match on the
   connection. Used in packet start message to TCM. */
#define PRS_REG_NO_MATCH_EVENT_ID				 0x40070
/* [ST 24] The number of input CFC flush packets */
#define PRS_REG_NUM_OF_CFC_FLUSH_MESSAGES			 0x40128
/* [ST 32] The number of cycles the Parser halted its operation since it
   could not allocate the next serial number */
#define PRS_REG_NUM_OF_DEAD_CYCLES				 0x40130
/* [ST 24] The number of input packets */
#define PRS_REG_NUM_OF_PACKETS					 0x40124
/* [ST 24] The number of input transparent flush packets */
#define PRS_REG_NUM_OF_TRANSPARENT_FLUSH_MESSAGES		 0x4012c
/* [RW 8] Context region for received Ethernet packet with a match and
   packet type 0. Used in CFC load request message */
#define PRS_REG_PACKET_REGIONS_TYPE_0				 0x40028
#define PRS_REG_PACKET_REGIONS_TYPE_1				 0x4002c
#define PRS_REG_PACKET_REGIONS_TYPE_2				 0x40030
#define PRS_REG_PACKET_REGIONS_TYPE_3				 0x40034
#define PRS_REG_PACKET_REGIONS_TYPE_4				 0x40038
#define PRS_REG_PACKET_REGIONS_TYPE_5				 0x4003c
#define PRS_REG_PACKET_REGIONS_TYPE_6				 0x40040
#define PRS_REG_PACKET_REGIONS_TYPE_7				 0x40044
/* [R 2] debug only: Number of pending requests for CAC on port 0. */
#define PRS_REG_PENDING_BRB_CAC0_RQ				 0x40174
/* [R 2] debug only: Number of pending requests for header parsing. */
#define PRS_REG_PENDING_BRB_PRS_RQ				 0x40170
/* [R 1] Interrupt register #0 read */
#define PRS_REG_PRS_INT_STS					 0x40188
/* [RW 8] Parity mask register #0 read/write */
#define PRS_REG_PRS_PRTY_MASK					 0x401a4
/* [R 8] Parity register #0 read */
#define PRS_REG_PRS_PRTY_STS					 0x40198
/* [RC 8] Parity register #0 read clear */
#define PRS_REG_PRS_PRTY_STS_CLR				 0x4019c
/* [RW 8] Context region for pure acknowledge packets. Used in CFC load
   request message */
#define PRS_REG_PURE_REGIONS					 0x40024
/* [R 32] debug only: Serial number status lsb 32 bits. '1' indicates this
   serail number was released by SDM but cannot be used because a previous
   serial number was not released. */
#define PRS_REG_SERIAL_NUM_STATUS_LSB				 0x40154
/* [R 32] debug only: Serial number status msb 32 bits. '1' indicates this
   serail number was released by SDM but cannot be used because a previous
   serial number was not released. */
#define PRS_REG_SERIAL_NUM_STATUS_MSB				 0x40158
/* [R 4] debug only: SRC current credit. Transaction based. */
#define PRS_REG_SRC_CURRENT_CREDIT				 0x4016c
/* [RW 16] The Ethernet type value for L2 tag 0 */
#define PRS_REG_TAG_ETHERTYPE_0					 0x401d4
/* [RW 4] The length of the info field for L2 tag 0. The length is between
 * 2B and 14B; in 2B granularity */
#define PRS_REG_TAG_LEN_0					 0x4022c
/* [R 8] debug only: TCM current credit. Cycle based. */
#define PRS_REG_TCM_CURRENT_CREDIT				 0x40160
/* [R 8] debug only: TSDM current credit. Transaction based. */
#define PRS_REG_TSDM_CURRENT_CREDIT				 0x4015c
#define PXP2_PXP2_INT_MASK_0_REG_PGL_CPL_AFT			 (0x1<<19)
#define PXP2_PXP2_INT_MASK_0_REG_PGL_CPL_OF			 (0x1<<20)
#define PXP2_PXP2_INT_MASK_0_REG_PGL_PCIE_ATTN			 (0x1<<22)
#define PXP2_PXP2_INT_MASK_0_REG_PGL_READ_BLOCKED		 (0x1<<23)
#define PXP2_PXP2_INT_MASK_0_REG_PGL_WRITE_BLOCKED		 (0x1<<24)
#define PXP2_PXP2_INT_STS_0_REG_WR_PGLUE_EOP_ERROR		 (0x1<<7)
#define PXP2_PXP2_INT_STS_CLR_0_REG_WR_PGLUE_EOP_ERROR		 (0x1<<7)
/* [R 6] Debug only: Number of used entries in the data FIFO */
#define PXP2_REG_HST_DATA_FIFO_STATUS				 0x12047c
/* [R 7] Debug only: Number of used entries in the header FIFO */
#define PXP2_REG_HST_HEADER_FIFO_STATUS				 0x120478
#define PXP2_REG_PGL_ADDR_88_F0					 0x120534
/* [R 32] GRC address for configuration access to PCIE config address 0x88.
 * any write to this PCIE address will cause a GRC write access to the
 * address that's in t this register */
#define PXP2_REG_PGL_ADDR_88_F1					 0x120544
#define PXP2_REG_PGL_ADDR_8C_F0					 0x120538
/* [R 32] GRC address for configuration access to PCIE config address 0x8c.
 * any write to this PCIE address will cause a GRC write access to the
 * address that's in t this register */
#define PXP2_REG_PGL_ADDR_8C_F1					 0x120548
#define PXP2_REG_PGL_ADDR_90_F0					 0x12053c
/* [R 32] GRC address for configuration access to PCIE config address 0x90.
 * any write to this PCIE address will cause a GRC write access to the
 * address that's in t this register */
#define PXP2_REG_PGL_ADDR_90_F1					 0x12054c
#define PXP2_REG_PGL_ADDR_94_F0					 0x120540
/* [R 32] GRC address for configuration access to PCIE config address 0x94.
 * any write to this PCIE address will cause a GRC write access to the
 * address that's in t this register */
#define PXP2_REG_PGL_ADDR_94_F1					 0x120550
#define PXP2_REG_PGL_CONTROL0					 0x120490
#define PXP2_REG_PGL_CONTROL1					 0x120514
#define PXP2_REG_PGL_DEBUG					 0x120520
/* [RW 32] third dword data of expansion rom request. this register is
   special. reading from it provides a vector outstanding read requests. if
   a bit is zero it means that a read request on the corresponding tag did
   not finish yet (not all completions have arrived for it) */
#define PXP2_REG_PGL_EXP_ROM2					 0x120808
/* [RW 32] Inbound interrupt table for CSDM: bits[31:16]-mask;
   its[15:0]-address */
#define PXP2_REG_PGL_INT_CSDM_0 				 0x1204f4
#define PXP2_REG_PGL_INT_CSDM_1 				 0x1204f8
#define PXP2_REG_PGL_INT_CSDM_2 				 0x1204fc
#define PXP2_REG_PGL_INT_CSDM_3 				 0x120500
#define PXP2_REG_PGL_INT_CSDM_4 				 0x120504
#define PXP2_REG_PGL_INT_CSDM_5 				 0x120508
#define PXP2_REG_PGL_INT_CSDM_6 				 0x12050c
#define PXP2_REG_PGL_INT_CSDM_7 				 0x120510
/* [RW 32] Inbound interrupt table for TSDM: bits[31:16]-mask;
   its[15:0]-address */
#define PXP2_REG_PGL_INT_TSDM_0 				 0x120494
#define PXP2_REG_PGL_INT_TSDM_1 				 0x120498
#define PXP2_REG_PGL_INT_TSDM_2 				 0x12049c
#define PXP2_REG_PGL_INT_TSDM_3 				 0x1204a0
#define PXP2_REG_PGL_INT_TSDM_4 				 0x1204a4
#define PXP2_REG_PGL_INT_TSDM_5 				 0x1204a8
#define PXP2_REG_PGL_INT_TSDM_6 				 0x1204ac
#define PXP2_REG_PGL_INT_TSDM_7 				 0x1204b0
/* [RW 32] Inbound interrupt table for USDM: bits[31:16]-mask;
   its[15:0]-address */
#define PXP2_REG_PGL_INT_USDM_0 				 0x1204b4
#define PXP2_REG_PGL_INT_USDM_1 				 0x1204b8
#define PXP2_REG_PGL_INT_USDM_2 				 0x1204bc
#define PXP2_REG_PGL_INT_USDM_3 				 0x1204c0
#define PXP2_REG_PGL_INT_USDM_4 				 0x1204c4
#define PXP2_REG_PGL_INT_USDM_5 				 0x1204c8
#define PXP2_REG_PGL_INT_USDM_6 				 0x1204cc
#define PXP2_REG_PGL_INT_USDM_7 				 0x1204d0
/* [RW 32] Inbound interrupt table for XSDM: bits[31:16]-mask;
   its[15:0]-address */
#define PXP2_REG_PGL_INT_XSDM_0 				 0x1204d4
#define PXP2_REG_PGL_INT_XSDM_1 				 0x1204d8
#define PXP2_REG_PGL_INT_XSDM_2 				 0x1204dc
#define PXP2_REG_PGL_INT_XSDM_3 				 0x1204e0
#define PXP2_REG_PGL_INT_XSDM_4 				 0x1204e4
#define PXP2_REG_PGL_INT_XSDM_5 				 0x1204e8
#define PXP2_REG_PGL_INT_XSDM_6 				 0x1204ec
#define PXP2_REG_PGL_INT_XSDM_7 				 0x1204f0
/* [RW 3] this field allows one function to pretend being another function
   when accessing any BAR mapped resource within the device. the value of
   the field is the number of the function that will be accessed
   effectively. after software write to this bit it must read it in order to
   know that the new value is updated */
#define PXP2_REG_PGL_PRETEND_FUNC_F0				 0x120674
#define PXP2_REG_PGL_PRETEND_FUNC_F1				 0x120678
#define PXP2_REG_PGL_PRETEND_FUNC_F2				 0x12067c
#define PXP2_REG_PGL_PRETEND_FUNC_F3				 0x120680
#define PXP2_REG_PGL_PRETEND_FUNC_F4				 0x120684
#define PXP2_REG_PGL_PRETEND_FUNC_F5				 0x120688
#define PXP2_REG_PGL_PRETEND_FUNC_F6				 0x12068c
#define PXP2_REG_PGL_PRETEND_FUNC_F7				 0x120690
/* [R 1] this bit indicates that a read request was blocked because of
   bus_master_en was deasserted */
#define PXP2_REG_PGL_READ_BLOCKED				 0x120568
#define PXP2_REG_PGL_TAGS_LIMIT 				 0x1205a8
/* [R 18] debug only */
#define PXP2_REG_PGL_TXW_CDTS					 0x12052c
/* [R 1] this bit indicates that a write request was blocked because of
   bus_master_en was deasserted */
#define PXP2_REG_PGL_WRITE_BLOCKED				 0x120564
#define PXP2_REG_PSWRQ_BW_ADD1					 0x1201c0
#define PXP2_REG_PSWRQ_BW_ADD10 				 0x1201e4
#define PXP2_REG_PSWRQ_BW_ADD11 				 0x1201e8
#define PXP2_REG_PSWRQ_BW_ADD2					 0x1201c4
#define PXP2_REG_PSWRQ_BW_ADD28 				 0x120228
#define PXP2_REG_PSWRQ_BW_ADD3					 0x1201c8
#define PXP2_REG_PSWRQ_BW_ADD6					 0x1201d4
#define PXP2_REG_PSWRQ_BW_ADD7					 0x1201d8
#define PXP2_REG_PSWRQ_BW_ADD8					 0x1201dc
#define PXP2_REG_PSWRQ_BW_ADD9					 0x1201e0
#define PXP2_REG_PSWRQ_BW_CREDIT				 0x12032c
#define PXP2_REG_PSWRQ_BW_L1					 0x1202b0
#define PXP2_REG_PSWRQ_BW_L10					 0x1202d4
#define PXP2_REG_PSWRQ_BW_L11					 0x1202d8
#define PXP2_REG_PSWRQ_BW_L2					 0x1202b4
#define PXP2_REG_PSWRQ_BW_L28					 0x120318
#define PXP2_REG_PSWRQ_BW_L3					 0x1202b8
#define PXP2_REG_PSWRQ_BW_L6					 0x1202c4
#define PXP2_REG_PSWRQ_BW_L7					 0x1202c8
#define PXP2_REG_PSWRQ_BW_L8					 0x1202cc
#define PXP2_REG_PSWRQ_BW_L9					 0x1202d0
#define PXP2_REG_PSWRQ_BW_RD					 0x120324
#define PXP2_REG_PSWRQ_BW_UB1					 0x120238
#define PXP2_REG_PSWRQ_BW_UB10					 0x12025c
#define PXP2_REG_PSWRQ_BW_UB11					 0x120260
#define PXP2_REG_PSWRQ_BW_UB2					 0x12023c
#define PXP2_REG_PSWRQ_BW_UB28					 0x1202a0
#define PXP2_REG_PSWRQ_BW_UB3					 0x120240
#define PXP2_REG_PSWRQ_BW_UB6					 0x12024c
#define PXP2_REG_PSWRQ_BW_UB7					 0x120250
#define PXP2_REG_PSWRQ_BW_UB8					 0x120254
#define PXP2_REG_PSWRQ_BW_UB9					 0x120258
#define PXP2_REG_PSWRQ_BW_WR					 0x120328
#define PXP2_REG_PSWRQ_CDU0_L2P 				 0x120000
#define PXP2_REG_PSWRQ_QM0_L2P					 0x120038
#define PXP2_REG_PSWRQ_SRC0_L2P 				 0x120054
#define PXP2_REG_PSWRQ_TM0_L2P					 0x12001c
#define PXP2_REG_PSWRQ_TSDM0_L2P				 0x1200e0
/* [RW 32] Interrupt mask register #0 read/write */
#define PXP2_REG_PXP2_INT_MASK_0				 0x120578
/* [R 32] Interrupt register #0 read */
#define PXP2_REG_PXP2_INT_STS_0 				 0x12056c
#define PXP2_REG_PXP2_INT_STS_1 				 0x120608
/* [RC 32] Interrupt register #0 read clear */
#define PXP2_REG_PXP2_INT_STS_CLR_0				 0x120570
/* [RW 32] Parity mask register #0 read/write */
#define PXP2_REG_PXP2_PRTY_MASK_0				 0x120588
#define PXP2_REG_PXP2_PRTY_MASK_1				 0x120598
/* [R 32] Parity register #0 read */
#define PXP2_REG_PXP2_PRTY_STS_0				 0x12057c
#define PXP2_REG_PXP2_PRTY_STS_1				 0x12058c
/* [RC 32] Parity register #0 read clear */
#define PXP2_REG_PXP2_PRTY_STS_CLR_0				 0x120580
#define PXP2_REG_PXP2_PRTY_STS_CLR_1				 0x120590
/* [R 1] Debug only: The 'almost full' indication from each fifo (gives
   indication about backpressure) */
#define PXP2_REG_RD_ALMOST_FULL_0				 0x120424
/* [R 8] Debug only: The blocks counter - number of unused block ids */
#define PXP2_REG_RD_BLK_CNT					 0x120418
/* [RW 8] Debug only: Total number of available blocks in Tetris Buffer.
   Must be bigger than 6. Normally should not be changed. */
#define PXP2_REG_RD_BLK_NUM_CFG 				 0x12040c
/* [RW 2] CDU byte swapping mode configuration for master read requests */
#define PXP2_REG_RD_CDURD_SWAP_MODE				 0x120404
/* [RW 1] When '1'; inputs to the PSWRD block are ignored */
#define PXP2_REG_RD_DISABLE_INPUTS				 0x120374
/* [R 1] PSWRD internal memories initialization is done */
#define PXP2_REG_RD_INIT_DONE					 0x120370
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq10 */
#define PXP2_REG_RD_MAX_BLKS_VQ10				 0x1203a0
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq11 */
#define PXP2_REG_RD_MAX_BLKS_VQ11				 0x1203a4
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq17 */
#define PXP2_REG_RD_MAX_BLKS_VQ17				 0x1203bc
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq18 */
#define PXP2_REG_RD_MAX_BLKS_VQ18				 0x1203c0
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq19 */
#define PXP2_REG_RD_MAX_BLKS_VQ19				 0x1203c4
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq22 */
#define PXP2_REG_RD_MAX_BLKS_VQ22				 0x1203d0
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq25 */
#define PXP2_REG_RD_MAX_BLKS_VQ25				 0x1203dc
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq6 */
#define PXP2_REG_RD_MAX_BLKS_VQ6				 0x120390
/* [RW 8] The maximum number of blocks in Tetris Buffer that can be
   allocated for vq9 */
#define PXP2_REG_RD_MAX_BLKS_VQ9				 0x12039c
/* [RW 2] PBF byte swapping mode configuration for master read requests */
#define PXP2_REG_RD_PBF_SWAP_MODE				 0x1203f4
/* [R 1] Debug only: Indication if delivery ports are idle */
#define PXP2_REG_RD_PORT_IS_IDLE_0				 0x12041c
#define PXP2_REG_RD_PORT_IS_IDLE_1				 0x120420
/* [RW 2] QM byte swapping mode configuration for master read requests */
#define PXP2_REG_RD_QM_SWAP_MODE				 0x1203f8
/* [R 7] Debug only: The SR counter - number of unused sub request ids */
#define PXP2_REG_RD_SR_CNT					 0x120414
/* [RW 2] SRC byte swapping mode configuration for master read requests */
#define PXP2_REG_RD_SRC_SWAP_MODE				 0x120400
/* [RW 7] Debug only: Total number of available PCI read sub-requests. Must
   be bigger than 1. Normally should not be changed. */
#define PXP2_REG_RD_SR_NUM_CFG					 0x120408
/* [RW 1] Signals the PSWRD block to start initializing internal memories */
#define PXP2_REG_RD_START_INIT					 0x12036c
/* [RW 2] TM byte swapping mode configuration for master read requests */
#define PXP2_REG_RD_TM_SWAP_MODE				 0x1203fc
/* [RW 10] Bandwidth addition to VQ0 write requests */
#define PXP2_REG_RQ_BW_RD_ADD0					 0x1201bc
/* [RW 10] Bandwidth addition to VQ12 read requests */
#define PXP2_REG_RQ_BW_RD_ADD12 				 0x1201ec
/* [RW 10] Bandwidth addition to VQ13 read requests */
#define PXP2_REG_RQ_BW_RD_ADD13 				 0x1201f0
/* [RW 10] Bandwidth addition to VQ14 read requests */
#define PXP2_REG_RQ_BW_RD_ADD14 				 0x1201f4
/* [RW 10] Bandwidth addition to VQ15 read requests */
#define PXP2_REG_RQ_BW_RD_ADD15 				 0x1201f8
/* [RW 10] Bandwidth addition to VQ16 read requests */
#define PXP2_REG_RQ_BW_RD_ADD16 				 0x1201fc
/* [RW 10] Bandwidth addition to VQ17 read requests */
#define PXP2_REG_RQ_BW_RD_ADD17 				 0x120200
/* [RW 10] Bandwidth addition to VQ18 read requests */
#define PXP2_REG_RQ_BW_RD_ADD18 				 0x120204
/* [RW 10] Bandwidth addition to VQ19 read requests */
#define PXP2_REG_RQ_BW_RD_ADD19 				 0x120208
/* [RW 10] Bandwidth addition to VQ20 read requests */
#define PXP2_REG_RQ_BW_RD_ADD20 				 0x12020c
/* [RW 10] Bandwidth addition to VQ22 read requests */
#define PXP2_REG_RQ_BW_RD_ADD22 				 0x120210
/* [RW 10] Bandwidth addition to VQ23 read requests */
#define PXP2_REG_RQ_BW_RD_ADD23 				 0x120214
/* [RW 10] Bandwidth addition to VQ24 read requests */
#define PXP2_REG_RQ_BW_RD_ADD24 				 0x120218
/* [RW 10] Bandwidth addition to VQ25 read requests */
#define PXP2_REG_RQ_BW_RD_ADD25 				 0x12021c
/* [RW 10] Bandwidth addition to VQ26 read requests */
#define PXP2_REG_RQ_BW_RD_ADD26 				 0x120220
/* [RW 10] Bandwidth addition to VQ27 read requests */
#define PXP2_REG_RQ_BW_RD_ADD27 				 0x120224
/* [RW 10] Bandwidth addition to VQ4 read requests */
#define PXP2_REG_RQ_BW_RD_ADD4					 0x1201cc
/* [RW 10] Bandwidth addition to VQ5 read requests */
#define PXP2_REG_RQ_BW_RD_ADD5					 0x1201d0
/* [RW 10] Bandwidth Typical L for VQ0 Read requests */
#define PXP2_REG_RQ_BW_RD_L0					 0x1202ac
/* [RW 10] Bandwidth Typical L for VQ12 Read requests */
#define PXP2_REG_RQ_BW_RD_L12					 0x1202dc
/* [RW 10] Bandwidth Typical L for VQ13 Read requests */
#define PXP2_REG_RQ_BW_RD_L13					 0x1202e0
/* [RW 10] Bandwidth Typical L for VQ14 Read requests */
#define PXP2_REG_RQ_BW_RD_L14					 0x1202e4
/* [RW 10] Bandwidth Typical L for VQ15 Read requests */
#define PXP2_REG_RQ_BW_RD_L15					 0x1202e8
/* [RW 10] Bandwidth Typical L for VQ16 Read requests */
#define PXP2_REG_RQ_BW_RD_L16					 0x1202ec
/* [RW 10] Bandwidth Typical L for VQ17 Read requests */
#define PXP2_REG_RQ_BW_RD_L17					 0x1202f0
/* [RW 10] Bandwidth Typical L for VQ18 Read requests */
#define PXP2_REG_RQ_BW_RD_L18					 0x1202f4
/* [RW 10] Bandwidth Typical L for VQ19 Read requests */
#define PXP2_REG_RQ_BW_RD_L19					 0x1202f8
/* [RW 10] Bandwidth Typical L for VQ20 Read requests */
#define PXP2_REG_RQ_BW_RD_L20					 0x1202fc
/* [RW 10] Bandwidth Typical L for VQ22 Read requests */
#define PXP2_REG_RQ_BW_RD_L22					 0x120300
/* [RW 10] Bandwidth Typical L for VQ23 Read requests */
#define PXP2_REG_RQ_BW_RD_L23					 0x120304
/* [RW 10] Bandwidth Typical L for VQ24 Read requests */
#define PXP2_REG_RQ_BW_RD_L24					 0x120308
/* [RW 10] Bandwidth Typical L for VQ25 Read requests */
#define PXP2_REG_RQ_BW_RD_L25					 0x12030c
/* [RW 10] Bandwidth Typical L for VQ26 Read requests */
#define PXP2_REG_RQ_BW_RD_L26					 0x120310
/* [RW 10] Bandwidth Typical L for VQ27 Read requests */
#define PXP2_REG_RQ_BW_RD_L27					 0x120314
/* [RW 10] Bandwidth Typical L for VQ4 Read requests */
#define PXP2_REG_RQ_BW_RD_L4					 0x1202bc
/* [RW 10] Bandwidth Typical L for VQ5 Read- currently not used */
#define PXP2_REG_RQ_BW_RD_L5					 0x1202c0
/* [RW 7] Bandwidth upper bound for VQ0 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND0				 0x120234
/* [RW 7] Bandwidth upper bound for VQ12 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND12				 0x120264
/* [RW 7] Bandwidth upper bound for VQ13 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND13				 0x120268
/* [RW 7] Bandwidth upper bound for VQ14 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND14				 0x12026c
/* [RW 7] Bandwidth upper bound for VQ15 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND15				 0x120270
/* [RW 7] Bandwidth upper bound for VQ16 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND16				 0x120274
/* [RW 7] Bandwidth upper bound for VQ17 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND17				 0x120278
/* [RW 7] Bandwidth upper bound for VQ18 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND18				 0x12027c
/* [RW 7] Bandwidth upper bound for VQ19 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND19				 0x120280
/* [RW 7] Bandwidth upper bound for VQ20 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND20				 0x120284
/* [RW 7] Bandwidth upper bound for VQ22 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND22				 0x120288
/* [RW 7] Bandwidth upper bound for VQ23 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND23				 0x12028c
/* [RW 7] Bandwidth upper bound for VQ24 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND24				 0x120290
/* [RW 7] Bandwidth upper bound for VQ25 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND25				 0x120294
/* [RW 7] Bandwidth upper bound for VQ26 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND26				 0x120298
/* [RW 7] Bandwidth upper bound for VQ27 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND27				 0x12029c
/* [RW 7] Bandwidth upper bound for VQ4 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND4				 0x120244
/* [RW 7] Bandwidth upper bound for VQ5 read requests */
#define PXP2_REG_RQ_BW_RD_UBOUND5				 0x120248
/* [RW 10] Bandwidth addition to VQ29 write requests */
#define PXP2_REG_RQ_BW_WR_ADD29 				 0x12022c
/* [RW 10] Bandwidth addition to VQ30 write requests */
#define PXP2_REG_RQ_BW_WR_ADD30 				 0x120230
/* [RW 10] Bandwidth Typical L for VQ29 Write requests */
#define PXP2_REG_RQ_BW_WR_L29					 0x12031c
/* [RW 10] Bandwidth Typical L for VQ30 Write requests */
#define PXP2_REG_RQ_BW_WR_L30					 0x120320
/* [RW 7] Bandwidth upper bound for VQ29 */
#define PXP2_REG_RQ_BW_WR_UBOUND29				 0x1202a4
/* [RW 7] Bandwidth upper bound for VQ30 */
#define PXP2_REG_RQ_BW_WR_UBOUND30				 0x1202a8
/* [RW 18] external first_mem_addr field in L2P table for CDU module port 0 */
#define PXP2_REG_RQ_CDU0_EFIRST_MEM_ADDR			 0x120008
/* [RW 2] Endian mode for cdu */
#define PXP2_REG_RQ_CDU_ENDIAN_M				 0x1201a0
#define PXP2_REG_RQ_CDU_FIRST_ILT				 0x12061c
#define PXP2_REG_RQ_CDU_LAST_ILT				 0x120620
/* [RW 3] page size in L2P table for CDU module; -4k; -8k; -16k; -32k; -64k;
   -128k */
#define PXP2_REG_RQ_CDU_P_SIZE					 0x120018
/* [R 1] 1' indicates that the requester has finished its internal
   configuration */
#define PXP2_REG_RQ_CFG_DONE					 0x1201b4
/* [RW 2] Endian mode for debug */
#define PXP2_REG_RQ_DBG_ENDIAN_M				 0x1201a4
/* [RW 1] When '1'; requests will enter input buffers but wont get out
   towards the glue */
#define PXP2_REG_RQ_DISABLE_INPUTS				 0x120330
/* [RW 4] Determines alignment of write SRs when a request is split into
 * several SRs. 0 - 8B aligned. 1 - 64B aligned. 2 - 128B aligned. 3 - 256B
 * aligned. 4 - 512B aligned. */
#define PXP2_REG_RQ_DRAM_ALIGN					 0x1205b0
/* [RW 4] Determines alignment of read SRs when a request is split into
 * several SRs. 0 - 8B aligned. 1 - 64B aligned. 2 - 128B aligned. 3 - 256B
 * aligned. 4 - 512B aligned. */
#define PXP2_REG_RQ_DRAM_ALIGN_RD				 0x12092c
/* [RW 1] when set the new alignment method (E2) will be applied; when reset
 * the original alignment method (E1 E1H) will be applied */
#define PXP2_REG_RQ_DRAM_ALIGN_SEL				 0x120930
/* [RW 1] If 1 ILT failiue will not result in ELT access; An interrupt will
   be asserted */
#define PXP2_REG_RQ_ELT_DISABLE 				 0x12066c
/* [RW 2] Endian mode for hc */
#define PXP2_REG_RQ_HC_ENDIAN_M 				 0x1201a8
/* [RW 1] when '0' ILT logic will work as in A0; otherwise B0; for back
   compatibility needs; Note that different registers are used per mode */
#define PXP2_REG_RQ_ILT_MODE					 0x1205b4
/* [WB 53] Onchip address table */
#define PXP2_REG_RQ_ONCHIP_AT					 0x122000
/* [WB 53] Onchip address table - B0 */
#define PXP2_REG_RQ_ONCHIP_AT_B0				 0x128000
/* [RW 13] Pending read limiter threshold; in Dwords */
#define PXP2_REG_RQ_PDR_LIMIT					 0x12033c
/* [RW 2] Endian mode for qm */
#define PXP2_REG_RQ_QM_ENDIAN_M 				 0x120194
#define PXP2_REG_RQ_QM_FIRST_ILT				 0x120634
#define PXP2_REG_RQ_QM_LAST_ILT 				 0x120638
/* [RW 3] page size in L2P table for QM module; -4k; -8k; -16k; -32k; -64k;
   -128k */
#define PXP2_REG_RQ_QM_P_SIZE					 0x120050
/* [RW 1] 1' indicates that the RBC has finished configuring the PSWRQ */
#define PXP2_REG_RQ_RBC_DONE					 0x1201b0
/* [RW 3] Max burst size filed for read requests port 0; 000 - 128B;
   001:256B; 010: 512B; 11:1K:100:2K; 01:4K */
#define PXP2_REG_RQ_RD_MBS0					 0x120160
/* [RW 3] Max burst size filed for read requests port 1; 000 - 128B;
   001:256B; 010: 512B; 11:1K:100:2K; 01:4K */
#define PXP2_REG_RQ_RD_MBS1					 0x120168
/* [RW 2] Endian mode for src */
#define PXP2_REG_RQ_SRC_ENDIAN_M				 0x12019c
#define PXP2_REG_RQ_SRC_FIRST_ILT				 0x12063c
#define PXP2_REG_RQ_SRC_LAST_ILT				 0x120640
/* [RW 3] page size in L2P table for SRC module; -4k; -8k; -16k; -32k; -64k;
   -128k */
#define PXP2_REG_RQ_SRC_P_SIZE					 0x12006c
/* [RW 2] Endian mode for tm */
#define PXP2_REG_RQ_TM_ENDIAN_M 				 0x120198
#define PXP2_REG_RQ_TM_FIRST_ILT				 0x120644
#define PXP2_REG_RQ_TM_LAST_ILT 				 0x120648
/* [RW 3] page size in L2P table for TM module; -4k; -8k; -16k; -32k; -64k;
   -128k */
#define PXP2_REG_RQ_TM_P_SIZE					 0x120034
/* [R 5] Number of entries in the ufifo; his fifo has l2p completions */
#define PXP2_REG_RQ_UFIFO_NUM_OF_ENTRY				 0x12080c
/* [RW 18] external first_mem_addr field in L2P table for USDM module port 0 */
#define PXP2_REG_RQ_USDM0_EFIRST_MEM_ADDR			 0x120094
/* [R 8] Number of entries occupied by vq 0 in pswrq memory */
#define PXP2_REG_RQ_VQ0_ENTRY_CNT				 0x120810
/* [R 8] Number of entries occupied by vq 10 in pswrq memory */
#define PXP2_REG_RQ_VQ10_ENTRY_CNT				 0x120818
/* [R 8] Number of entries occupied by vq 11 in pswrq memory */
#define PXP2_REG_RQ_VQ11_ENTRY_CNT				 0x120820
/* [R 8] Number of entries occupied by vq 12 in pswrq memory */
#define PXP2_REG_RQ_VQ12_ENTRY_CNT				 0x120828
/* [R 8] Number of entries occupied by vq 13 in pswrq memory */
#define PXP2_REG_RQ_VQ13_ENTRY_CNT				 0x120830
/* [R 8] Number of entries occupied by vq 14 in pswrq memory */
#define PXP2_REG_RQ_VQ14_ENTRY_CNT				 0x120838
/* [R 8] Number of entries occupied by vq 15 in pswrq memory */
#define PXP2_REG_RQ_VQ15_ENTRY_CNT				 0x120840
/* [R 8] Number of entries occupied by vq 16 in pswrq memory */
#define PXP2_REG_RQ_VQ16_ENTRY_CNT				 0x120848
/* [R 8] Number of entries occupied by vq 17 in pswrq memory */
#define PXP2_REG_RQ_VQ17_ENTRY_CNT				 0x120850
/* [R 8] Number of entries occupied by vq 18 in pswrq memory */
#define PXP2_REG_RQ_VQ18_ENTRY_CNT				 0x120858
/* [R 8] Number of entries occupied by vq 19 in pswrq memory */
#define PXP2_REG_RQ_VQ19_ENTRY_CNT				 0x120860
/* [R 8] Number of entries occupied by vq 1 in pswrq memory */
#define PXP2_REG_RQ_VQ1_ENTRY_CNT				 0x120868
/* [R 8] Number of entries occupied by vq 20 in pswrq memory */
#define PXP2_REG_RQ_VQ20_ENTRY_CNT				 0x120870
/* [R 8] Number of entries occupied by vq 21 in pswrq memory */
#define PXP2_REG_RQ_VQ21_ENTRY_CNT				 0x120878
/* [R 8] Number of entries occupied by vq 22 in pswrq memory */
#define PXP2_REG_RQ_VQ22_ENTRY_CNT				 0x120880
/* [R 8] Number of entries occupied by vq 23 in pswrq memory */
#define PXP2_REG_RQ_VQ23_ENTRY_CNT				 0x120888
/* [R 8] Number of entries occupied by vq 24 in pswrq memory */
#define PXP2_REG_RQ_VQ24_ENTRY_CNT				 0x120890
/* [R 8] Number of entries occupied by vq 25 in pswrq memory */
#define PXP2_REG_RQ_VQ25_ENTRY_CNT				 0x120898
/* [R 8] Number of entries occupied by vq 26 in pswrq memory */
#define PXP2_REG_RQ_VQ26_ENTRY_CNT				 0x1208a0
/* [R 8] Number of entries occupied by vq 27 in pswrq memory */
#define PXP2_REG_RQ_VQ27_ENTRY_CNT				 0x1208a8
/* [R 8] Number of entries occupied by vq 28 in pswrq memory */
#define PXP2_REG_RQ_VQ28_ENTRY_CNT				 0x1208b0
/* [R 8] Number of entries occupied by vq 29 in pswrq memory */
#define PXP2_REG_RQ_VQ29_ENTRY_CNT				 0x1208b8
/* [R 8] Number of entries occupied by vq 2 in pswrq memory */
#define PXP2_REG_RQ_VQ2_ENTRY_CNT				 0x1208c0
/* [R 8] Number of entries occupied by vq 30 in pswrq memory */
#define PXP2_REG_RQ_VQ30_ENTRY_CNT				 0x1208c8
/* [R 8] Number of entries occupied by vq 31 in pswrq memory */
#define PXP2_REG_RQ_VQ31_ENTRY_CNT				 0x1208d0
/* [R 8] Number of entries occupied by vq 3 in pswrq memory */
#define PXP2_REG_RQ_VQ3_ENTRY_CNT				 0x1208d8
/* [R 8] Number of entries occupied by vq 4 in pswrq memory */
#define PXP2_REG_RQ_VQ4_ENTRY_CNT				 0x1208e0
/* [R 8] Number of entries occupied by vq 5 in pswrq memory */
#define PXP2_REG_RQ_VQ5_ENTRY_CNT				 0x1208e8
/* [R 8] Number of entries occupied by vq 6 in pswrq memory */
#define PXP2_REG_RQ_VQ6_ENTRY_CNT				 0x1208f0
/* [R 8] Number of entries occupied by vq 7 in pswrq memory */
#define PXP2_REG_RQ_VQ7_ENTRY_CNT				 0x1208f8
/* [R 8] Number of entries occupied by vq 8 in pswrq memory */
#define PXP2_REG_RQ_VQ8_ENTRY_CNT				 0x120900
/* [R 8] Number of entries occupied by vq 9 in pswrq memory */
#define PXP2_REG_RQ_VQ9_ENTRY_CNT				 0x120908
/* [RW 3] Max burst size filed for write requests port 0; 000 - 128B;
   001:256B; 010: 512B; */
#define PXP2_REG_RQ_WR_MBS0					 0x12015c
/* [RW 3] Max burst size filed for write requests port 1; 000 - 128B;
   001:256B; 010: 512B; */
#define PXP2_REG_RQ_WR_MBS1					 0x120164
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_CDU_MPS					 0x1205f0
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_CSDM_MPS					 0x1205d0
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_DBG_MPS					 0x1205e8
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_DMAE_MPS					 0x1205ec
/* [RW 10] if Number of entries in dmae fifo will be higher than this
   threshold then has_payload indication will be asserted; the default value
   should be equal to &gt;  write MBS size! */
#define PXP2_REG_WR_DMAE_TH					 0x120368
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_HC_MPS					 0x1205c8
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_QM_MPS					 0x1205dc
/* [RW 1] 0 - working in A0 mode;  - working in B0 mode */
#define PXP2_REG_WR_REV_MODE					 0x120670
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_SRC_MPS					 0x1205e4
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_TM_MPS					 0x1205e0
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_TSDM_MPS					 0x1205d4
/* [RW 10] if Number of entries in usdmdp fifo will be higher than this
   threshold then has_payload indication will be asserted; the default value
   should be equal to &gt;  write MBS size! */
#define PXP2_REG_WR_USDMDP_TH					 0x120348
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_USDM_MPS					 0x1205cc
/* [RW 2] 0 - 128B;  - 256B;  - 512B;  - 1024B; when the payload in the
   buffer reaches this number has_payload will be asserted */
#define PXP2_REG_WR_XSDM_MPS					 0x1205d8
/* [R 1] debug only: Indication if PSWHST arbiter is idle */
#define PXP_REG_HST_ARB_IS_IDLE 				 0x103004
/* [R 8] debug only: A bit mask for all PSWHST arbiter clients. '1' means
   this client is waiting for the arbiter. */
#define PXP_REG_HST_CLIENTS_WAITING_TO_ARB			 0x103008
/* [RW 1] When 1; doorbells are discarded and not passed to doorbell queue
   block. Should be used for close the gates. */
#define PXP_REG_HST_DISCARD_DOORBELLS				 0x1030a4
/* [R 1] debug only: '1' means this PSWHST is discarding doorbells. This bit
   should update according to 'hst_discard_doorbells' register when the state
   machine is idle */
#define PXP_REG_HST_DISCARD_DOORBELLS_STATUS			 0x1030a0
/* [RW 1] When 1; new internal writes arriving to the block are discarded.
   Should be used for close the gates. */
#define PXP_REG_HST_DISCARD_INTERNAL_WRITES			 0x1030a8
/* [R 6] debug only: A bit mask for all PSWHST internal write clients. '1'
   means this PSWHST is discarding inputs from this client. Each bit should
   update according to 'hst_discard_internal_writes' register when the state
   machine is idle. */
#define PXP_REG_HST_DISCARD_INTERNAL_WRITES_STATUS		 0x10309c
/* [WB 160] Used for initialization of the inbound interrupts memory */
#define PXP_REG_HST_INBOUND_INT 				 0x103800
/* [RW 32] Interrupt mask register #0 read/write */
#define PXP_REG_PXP_INT_MASK_0					 0x103074
#define PXP_REG_PXP_INT_MASK_1					 0x103084
/* [R 32] Interrupt register #0 read */
#define PXP_REG_PXP_INT_STS_0					 0x103068
#define PXP_REG_PXP_INT_STS_1					 0x103078
/* [RC 32] Interrupt register #0 read clear */
#define PXP_REG_PXP_INT_STS_CLR_0				 0x10306c
#define PXP_REG_PXP_INT_STS_CLR_1				 0x10307c
/* [RW 27] Parity mask register #0 read/write */
#define PXP_REG_PXP_PRTY_MASK					 0x103094
/* [R 26] Parity register #0 read */
#define PXP_REG_PXP_PRTY_STS					 0x103088
/* [RC 27] Parity register #0 read clear */
#define PXP_REG_PXP_PRTY_STS_CLR				 0x10308c
/* [RW 4] The activity counter initial increment value sent in the load
   request */
#define QM_REG_ACTCTRINITVAL_0					 0x168040
#define QM_REG_ACTCTRINITVAL_1					 0x168044
#define QM_REG_ACTCTRINITVAL_2					 0x168048
#define QM_REG_ACTCTRINITVAL_3					 0x16804c
/* [RW 32] The base logical address (in bytes) of each physical queue. The
   index I represents the physical queue number. The 12 lsbs are ignore and
   considered zero so practically there are only 20 bits in this register;
   queues 63-0 */
#define QM_REG_BASEADDR 					 0x168900
/* [RW 32] The base logical address (in bytes) of each physical queue. The
   index I represents the physical queue number. The 12 lsbs are ignore and
   considered zero so practically there are only 20 bits in this register;
   queues 127-64 */
#define QM_REG_BASEADDR_EXT_A					 0x16e100
/* [RW 16] The byte credit cost for each task. This value is for both ports */
#define QM_REG_BYTECRDCOST					 0x168234
/* [RW 16] The initial byte credit value for both ports. */
#define QM_REG_BYTECRDINITVAL					 0x168238
/* [RW 32] A bit per physical queue. If the bit is cleared then the physical
   queue uses port 0 else it uses port 1; queues 31-0 */
#define QM_REG_BYTECRDPORT_LSB					 0x168228
/* [RW 32] A bit per physical queue. If the bit is cleared then the physical
   queue uses port 0 else it uses port 1; queues 95-64 */
#define QM_REG_BYTECRDPORT_LSB_EXT_A				 0x16e520
/* [RW 32] A bit per physical queue. If the bit is cleared then the physical
   queue uses port 0 else it uses port 1; queues 63-32 */
#define QM_REG_BYTECRDPORT_MSB					 0x168224
/* [RW 32] A bit per physical queue. If the bit is cleared then the physical
   queue uses port 0 else it uses port 1; queues 127-96 */
#define QM_REG_BYTECRDPORT_MSB_EXT_A				 0x16e51c
/* [RW 16] The byte credit value that if above the QM is considered almost
   full */
#define QM_REG_BYTECREDITAFULLTHR				 0x168094
/* [RW 4] The initial credit for interface */
#define QM_REG_CMINITCRD_0					 0x1680cc
#define QM_REG_BYTECRDCMDQ_0					 0x16e6e8
#define QM_REG_CMINITCRD_1					 0x1680d0
#define QM_REG_CMINITCRD_2					 0x1680d4
#define QM_REG_CMINITCRD_3					 0x1680d8
#define QM_REG_CMINITCRD_4					 0x1680dc
#define QM_REG_CMINITCRD_5					 0x1680e0
#define QM_REG_CMINITCRD_6					 0x1680e4
#define QM_REG_CMINITCRD_7					 0x1680e8
/* [RW 8] A mask bit per CM interface. If this bit is 0 then this interface
   is masked */
#define QM_REG_CMINTEN						 0x1680ec
/* [RW 12] A bit vector which indicates which one of the queues are tied to
   interface 0 */
#define QM_REG_CMINTVOQMASK_0					 0x1681f4
#define QM_REG_CMINTVOQMASK_1					 0x1681f8
#define QM_REG_CMINTVOQMASK_2					 0x1681fc
#define QM_REG_CMINTVOQMASK_3					 0x168200
#define QM_REG_CMINTVOQMASK_4					 0x168204
#define QM_REG_CMINTVOQMASK_5					 0x168208
#define QM_REG_CMINTVOQMASK_6					 0x16820c
#define QM_REG_CMINTVOQMASK_7					 0x168210
/* [RW 20] The number of connections divided by 16 which dictates the size
   of each queue which belongs to even function number. */
#define QM_REG_CONNNUM_0					 0x168020
/* [R 6] Keep the fill level of the fifo from write client 4 */
#define QM_REG_CQM_WRC_FIFOLVL					 0x168018
/* [RW 8] The context regions sent in the CFC load request */
#define QM_REG_CTXREG_0 					 0x168030
#define QM_REG_CTXREG_1 					 0x168034
#define QM_REG_CTXREG_2 					 0x168038
#define QM_REG_CTXREG_3 					 0x16803c
/* [RW 12] The VOQ mask used to select the VOQs which needs to be full for
   bypass enable */
#define QM_REG_ENBYPVOQMASK					 0x16823c
/* [RW 32] A bit mask per each physical queue. If a bit is set then the
   physical queue uses the byte credit; queues 31-0 */
#define QM_REG_ENBYTECRD_LSB					 0x168220
/* [RW 32] A bit mask per each physical queue. If a bit is set then the
   physical queue uses the byte credit; queues 95-64 */
#define QM_REG_ENBYTECRD_LSB_EXT_A				 0x16e518
/* [RW 32] A bit mask per each physical queue. If a bit is set then the
   physical queue uses the byte credit; queues 63-32 */
#define QM_REG_ENBYTECRD_MSB					 0x16821c
/* [RW 32] A bit mask per each physical queue. If a bit is set then the
   physical queue uses the byte credit; queues 127-96 */
#define QM_REG_ENBYTECRD_MSB_EXT_A				 0x16e514
/* [RW 4] If cleared then the secondary interface will not be served by the
   RR arbiter */
#define QM_REG_ENSEC						 0x1680f0
/* [RW 32] NA */
#define QM_REG_FUNCNUMSEL_LSB					 0x168230
/* [RW 32] NA */
#define QM_REG_FUNCNUMSEL_MSB					 0x16822c
/* [RW 32] A mask register to mask the Almost empty signals which will not
   be use for the almost empty indication to the HW block; queues 31:0 */
#define QM_REG_HWAEMPTYMASK_LSB 				 0x168218
/* [RW 32] A mask register to mask the Almost empty signals which will not
   be use for the almost empty indication to the HW block; queues 95-64 */
#define QM_REG_HWAEMPTYMASK_LSB_EXT_A				 0x16e510
/* [RW 32] A mask register to mask the Almost empty signals which will not
   be use for the almost empty indication to the HW block; queues 63:32 */
#define QM_REG_HWAEMPTYMASK_MSB 				 0x168214
/* [RW 32] A mask register to mask the Almost empty signals which will not
   be use for the almost empty indication to the HW block; queues 127-96 */
#define QM_REG_HWAEMPTYMASK_MSB_EXT_A				 0x16e50c
/* [RW 4] The number of outstanding request to CFC */
#define QM_REG_OUTLDREQ 					 0x168804
/* [RC 1] A flag to indicate that overflow error occurred in one of the
   queues. */
#define QM_REG_OVFERROR 					 0x16805c
/* [RC 7] the Q where the overflow occurs */
#define QM_REG_OVFQNUM						 0x168058
/* [R 16] Pause state for physical queues 15-0 */
#define QM_REG_PAUSESTATE0					 0x168410
/* [R 16] Pause state for physical queues 31-16 */
#define QM_REG_PAUSESTATE1					 0x168414
/* [R 16] Pause state for physical queues 47-32 */
#define QM_REG_PAUSESTATE2					 0x16e684
/* [R 16] Pause state for physical queues 63-48 */
#define QM_REG_PAUSESTATE3					 0x16e688
/* [R 16] Pause state for physical queues 79-64 */
#define QM_REG_PAUSESTATE4					 0x16e68c
/* [R 16] Pause state for physical queues 95-80 */
#define QM_REG_PAUSESTATE5					 0x16e690
/* [R 16] Pause state for physical queues 111-96 */
#define QM_REG_PAUSESTATE6					 0x16e694
/* [R 16] Pause state for physical queues 127-112 */
#define QM_REG_PAUSESTATE7					 0x16e698
/* [RW 2] The PCI attributes field used in the PCI request. */
#define QM_REG_PCIREQAT 					 0x168054
#define QM_REG_PF_EN						 0x16e70c
/* [R 24] The number of tasks stored in the QM for the PF. only even
 * functions are valid in E2 (odd I registers will be hard wired to 0) */
#define QM_REG_PF_USG_CNT_0					 0x16e040
/* [R 16] NOT USED */
#define QM_REG_PORT0BYTECRD					 0x168300
/* [R 16] The byte credit of port 1 */
#define QM_REG_PORT1BYTECRD					 0x168304
/* [RW 3] pci function number of queues 15-0 */
#define QM_REG_PQ2PCIFUNC_0					 0x16e6bc
#define QM_REG_PQ2PCIFUNC_1					 0x16e6c0
#define QM_REG_PQ2PCIFUNC_2					 0x16e6c4
#define QM_REG_PQ2PCIFUNC_3					 0x16e6c8
#define QM_REG_PQ2PCIFUNC_4					 0x16e6cc
#define QM_REG_PQ2PCIFUNC_5					 0x16e6d0
#define QM_REG_PQ2PCIFUNC_6					 0x16e6d4
#define QM_REG_PQ2PCIFUNC_7					 0x16e6d8
/* [WB 54] Pointer Table Memory for queues 63-0; The mapping is as follow:
   ptrtbl[53:30] read pointer; ptrtbl[29:6] write pointer; ptrtbl[5:4] read
   bank0; ptrtbl[3:2] read bank 1; ptrtbl[1:0] write bank; */
#define QM_REG_PTRTBL						 0x168a00
/* [WB 54] Pointer Table Memory for queues 127-64; The mapping is as follow:
   ptrtbl[53:30] read pointer; ptrtbl[29:6] write pointer; ptrtbl[5:4] read
   bank0; ptrtbl[3:2] read bank 1; ptrtbl[1:0] write bank; */
#define QM_REG_PTRTBL_EXT_A					 0x16e200
/* [RW 2] Interrupt mask register #0 read/write */
#define QM_REG_QM_INT_MASK					 0x168444
/* [R 2] Interrupt register #0 read */
#define QM_REG_QM_INT_STS					 0x168438
/* [RW 12] Parity mask register #0 read/write */
#define QM_REG_QM_PRTY_MASK					 0x168454
/* [R 12] Parity register #0 read */
#define QM_REG_QM_PRTY_STS					 0x168448
/* [RC 12] Parity register #0 read clear */
#define QM_REG_QM_PRTY_STS_CLR					 0x16844c
/* [R 32] Current queues in pipeline: Queues from 32 to 63 */
#define QM_REG_QSTATUS_HIGH					 0x16802c
/* [R 32] Current queues in pipeline: Queues from 96 to 127 */
#define QM_REG_QSTATUS_HIGH_EXT_A				 0x16e408
/* [R 32] Current queues in pipeline: Queues from 0 to 31 */
#define QM_REG_QSTATUS_LOW					 0x168028
/* [R 32] Current queues in pipeline: Queues from 64 to 95 */
#define QM_REG_QSTATUS_LOW_EXT_A				 0x16e404
/* [R 24] The number of tasks queued for each queue; queues 63-0 */
#define QM_REG_QTASKCTR_0					 0x168308
/* [R 24] The number of tasks queued for each queue; queues 127-64 */
#define QM_REG_QTASKCTR_EXT_A_0 				 0x16e584
/* [RW 4] Queue tied to VOQ */
#define QM_REG_QVOQIDX_0					 0x1680f4
#define QM_REG_QVOQIDX_10					 0x16811c
#define QM_REG_QVOQIDX_100					 0x16e49c
#define QM_REG_QVOQIDX_101					 0x16e4a0
#define QM_REG_QVOQIDX_102					 0x16e4a4
#define QM_REG_QVOQIDX_103					 0x16e4a8
#define QM_REG_QVOQIDX_104					 0x16e4ac
#define QM_REG_QVOQIDX_105					 0x16e4b0
#define QM_REG_QVOQIDX_106					 0x16e4b4
#define QM_REG_QVOQIDX_107					 0x16e4b8
#define QM_REG_QVOQIDX_108					 0x16e4bc
#define QM_REG_QVOQIDX_109					 0x16e4c0
#define QM_REG_QVOQIDX_11					 0x168120
#define QM_REG_QVOQIDX_110					 0x16e4c4
#define QM_REG_QVOQIDX_111					 0x16e4c8
#define QM_REG_QVOQIDX_112					 0x16e4cc
#define QM_REG_QVOQIDX_113					 0x16e4d0
#define QM_REG_QVOQIDX_114					 0x16e4d4
#define QM_REG_QVOQIDX_115					 0x16e4d8
#define QM_REG_QVOQIDX_116					 0x16e4dc
#define QM_REG_QVOQIDX_117					 0x16e4e0
#define QM_REG_QVOQIDX_118					 0x16e4e4
#define QM_REG_QVOQIDX_119					 0x16e4e8
#define QM_REG_QVOQIDX_12					 0x168124
#define QM_REG_QVOQIDX_120					 0x16e4ec
#define QM_REG_QVOQIDX_121					 0x16e4f0
#define QM_REG_QVOQIDX_122					 0x16e4f4
#define QM_REG_QVOQIDX_123					 0x16e4f8
#define QM_REG_QVOQIDX_124					 0x16e4fc
#define QM_REG_QVOQIDX_125					 0x16e500
#define QM_REG_QVOQIDX_126					 0x16e504
#define QM_REG_QVOQIDX_127					 0x16e508
#define QM_REG_QVOQIDX_13					 0x168128
#define QM_REG_QVOQIDX_14					 0x16812c
#define QM_REG_QVOQIDX_15					 0x168130
#define QM_REG_QVOQIDX_16					 0x168134
#define QM_REG_QVOQIDX_17					 0x168138
#define QM_REG_QVOQIDX_21					 0x168148
#define QM_REG_QVOQIDX_22					 0x16814c
#define QM_REG_QVOQIDX_23					 0x168150
#define QM_REG_QVOQIDX_24					 0x168154
#define QM_REG_QVOQIDX_25					 0x168158
#define QM_REG_QVOQIDX_26					 0x16815c
#define QM_REG_QVOQIDX_27					 0x168160
#define QM_REG_QVOQIDX_28					 0x168164
#define QM_REG_QVOQIDX_29					 0x168168
#define QM_REG_QVOQIDX_30					 0x16816c
#define QM_REG_QVOQIDX_31					 0x168170
#define QM_REG_QVOQIDX_32					 0x168174
#define QM_REG_QVOQIDX_33					 0x168178
#define QM_REG_QVOQIDX_34					 0x16817c
#define QM_REG_QVOQIDX_35					 0x168180
#define QM_REG_QVOQIDX_36					 0x168184
#define QM_REG_QVOQIDX_37					 0x168188
#define QM_REG_QVOQIDX_38					 0x16818c
#define QM_REG_QVOQIDX_39					 0x168190
#define QM_REG_QVOQIDX_40					 0x168194
#define QM_REG_QVOQIDX_41					 0x168198
#define QM_REG_QVOQIDX_42					 0x16819c
#define QM_REG_QVOQIDX_43					 0x1681a0
#define QM_REG_QVOQIDX_44					 0x1681a4
#define QM_REG_QVOQIDX_45					 0x1681a8
#define QM_REG_QVOQIDX_46					 0x1681ac
#define QM_REG_QVOQIDX_47					 0x1681b0
#define QM_REG_QVOQIDX_48					 0x1681b4
#define QM_REG_QVOQIDX_49					 0x1681b8
#define QM_REG_QVOQIDX_5					 0x168108
#define QM_REG_QVOQIDX_50					 0x1681bc
#define QM_REG_QVOQIDX_51					 0x1681c0
#define QM_REG_QVOQIDX_52					 0x1681c4
#define QM_REG_QVOQIDX_53					 0x1681c8
#define QM_REG_QVOQIDX_54					 0x1681cc
#define QM_REG_QVOQIDX_55					 0x1681d0
#define QM_REG_QVOQIDX_56					 0x1681d4
#define QM_REG_QVOQIDX_57					 0x1681d8
#define QM_REG_QVOQIDX_58					 0x1681dc
#define QM_REG_QVOQIDX_59					 0x1681e0
#define QM_REG_QVOQIDX_6					 0x16810c
#define QM_REG_QVOQIDX_60					 0x1681e4
#define QM_REG_QVOQIDX_61					 0x1681e8
#define QM_REG_QVOQIDX_62					 0x1681ec
#define QM_REG_QVOQIDX_63					 0x1681f0
#define QM_REG_QVOQIDX_64					 0x16e40c
#define QM_REG_QVOQIDX_65					 0x16e410
#define QM_REG_QVOQIDX_69					 0x16e420
#define QM_REG_QVOQIDX_7					 0x168110
#define QM_REG_QVOQIDX_70					 0x16e424
#define QM_REG_QVOQIDX_71					 0x16e428
#define QM_REG_QVOQIDX_72					 0x16e42c
#define QM_REG_QVOQIDX_73					 0x16e430
#define QM_REG_QVOQIDX_74					 0x16e434
#define QM_REG_QVOQIDX_75					 0x16e438
#define QM_REG_QVOQIDX_76					 0x16e43c
#define QM_REG_QVOQIDX_77					 0x16e440
#define QM_REG_QVOQIDX_78					 0x16e444
#define QM_REG_QVOQIDX_79					 0x16e448
#define QM_REG_QVOQIDX_8					 0x168114
#define QM_REG_QVOQIDX_80					 0x16e44c
#define QM_REG_QVOQIDX_81					 0x16e450
#define QM_REG_QVOQIDX_85					 0x16e460
#define QM_REG_QVOQIDX_86					 0x16e464
#define QM_REG_QVOQIDX_87					 0x16e468
#define QM_REG_QVOQIDX_88					 0x16e46c
#define QM_REG_QVOQIDX_89					 0x16e470
#define QM_REG_QVOQIDX_9					 0x168118
#define QM_REG_QVOQIDX_90					 0x16e474
#define QM_REG_QVOQIDX_91					 0x16e478
#define QM_REG_QVOQIDX_92					 0x16e47c
#define QM_REG_QVOQIDX_93					 0x16e480
#define QM_REG_QVOQIDX_94					 0x16e484
#define QM_REG_QVOQIDX_95					 0x16e488
#define QM_REG_QVOQIDX_96					 0x16e48c
#define QM_REG_QVOQIDX_97					 0x16e490
#define QM_REG_QVOQIDX_98					 0x16e494
#define QM_REG_QVOQIDX_99					 0x16e498
/* [RW 1] Initialization bit command */
#define QM_REG_SOFT_RESET					 0x168428
/* [RW 8] The credit cost per every task in the QM. A value per each VOQ */
#define QM_REG_TASKCRDCOST_0					 0x16809c
#define QM_REG_TASKCRDCOST_1					 0x1680a0
#define QM_REG_TASKCRDCOST_2					 0x1680a4
#define QM_REG_TASKCRDCOST_4					 0x1680ac
#define QM_REG_TASKCRDCOST_5					 0x1680b0
/* [R 6] Keep the fill level of the fifo from write client 3 */
#define QM_REG_TQM_WRC_FIFOLVL					 0x168010
/* [R 6] Keep the fill level of the fifo from write client 2 */
#define QM_REG_UQM_WRC_FIFOLVL					 0x168008
/* [RC 32] Credit update error register */
#define QM_REG_VOQCRDERRREG					 0x168408
/* [R 16] The credit value for each VOQ */
#define QM_REG_VOQCREDIT_0					 0x1682d0
#define QM_REG_VOQCREDIT_1					 0x1682d4
#define QM_REG_VOQCREDIT_4					 0x1682e0
/* [RW 16] The credit value that if above the QM is considered almost full */
#define QM_REG_VOQCREDITAFULLTHR				 0x168090
/* [RW 16] The init and maximum credit for each VoQ */
#define QM_REG_VOQINITCREDIT_0					 0x168060
#define QM_REG_VOQINITCREDIT_1					 0x168064
#define QM_REG_VOQINITCREDIT_2					 0x168068
#define QM_REG_VOQINITCREDIT_4					 0x168070
#define QM_REG_VOQINITCREDIT_5					 0x168074
/* [RW 1] The port of which VOQ belongs */
#define QM_REG_VOQPORT_0					 0x1682a0
#define QM_REG_VOQPORT_1					 0x1682a4
#define QM_REG_VOQPORT_2					 0x1682a8
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_0_LSB					 0x168240
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_0_LSB_EXT_A				 0x16e524
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_0_MSB					 0x168244
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_0_MSB_EXT_A				 0x16e528
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_10_LSB					 0x168290
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_10_LSB_EXT_A				 0x16e574
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_10_MSB					 0x168294
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_10_MSB_EXT_A				 0x16e578
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_11_LSB					 0x168298
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_11_LSB_EXT_A				 0x16e57c
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_11_MSB					 0x16829c
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_11_MSB_EXT_A				 0x16e580
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_1_LSB					 0x168248
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_1_LSB_EXT_A				 0x16e52c
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_1_MSB					 0x16824c
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_1_MSB_EXT_A				 0x16e530
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_2_LSB					 0x168250
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_2_LSB_EXT_A				 0x16e534
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_2_MSB					 0x168254
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_2_MSB_EXT_A				 0x16e538
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_3_LSB					 0x168258
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_3_LSB_EXT_A				 0x16e53c
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_3_MSB_EXT_A				 0x16e540
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_4_LSB					 0x168260
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_4_LSB_EXT_A				 0x16e544
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_4_MSB					 0x168264
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_4_MSB_EXT_A				 0x16e548
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_5_LSB					 0x168268
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_5_LSB_EXT_A				 0x16e54c
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_5_MSB					 0x16826c
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_5_MSB_EXT_A				 0x16e550
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_6_LSB					 0x168270
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_6_LSB_EXT_A				 0x16e554
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_6_MSB					 0x168274
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_6_MSB_EXT_A				 0x16e558
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_7_LSB					 0x168278
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_7_LSB_EXT_A				 0x16e55c
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_7_MSB					 0x16827c
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_7_MSB_EXT_A				 0x16e560
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_8_LSB					 0x168280
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_8_LSB_EXT_A				 0x16e564
/* [RW 32] The physical queue number associated with each VOQ; queues 63-32 */
#define QM_REG_VOQQMASK_8_MSB					 0x168284
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_8_MSB_EXT_A				 0x16e568
/* [RW 32] The physical queue number associated with each VOQ; queues 31-0 */
#define QM_REG_VOQQMASK_9_LSB					 0x168288
/* [RW 32] The physical queue number associated with each VOQ; queues 95-64 */
#define QM_REG_VOQQMASK_9_LSB_EXT_A				 0x16e56c
/* [RW 32] The physical queue number associated with each VOQ; queues 127-96 */
#define QM_REG_VOQQMASK_9_MSB_EXT_A				 0x16e570
/* [RW 32] Wrr weights */
#define QM_REG_WRRWEIGHTS_0					 0x16880c
#define QM_REG_WRRWEIGHTS_1					 0x168810
#define QM_REG_WRRWEIGHTS_10					 0x168814
#define QM_REG_WRRWEIGHTS_11					 0x168818
#define QM_REG_WRRWEIGHTS_12					 0x16881c
#define QM_REG_WRRWEIGHTS_13					 0x168820
#define QM_REG_WRRWEIGHTS_14					 0x168824
#define QM_REG_WRRWEIGHTS_15					 0x168828
#define QM_REG_WRRWEIGHTS_16					 0x16e000
#define QM_REG_WRRWEIGHTS_17					 0x16e004
#define QM_REG_WRRWEIGHTS_18					 0x16e008
#define QM_REG_WRRWEIGHTS_19					 0x16e00c
#define QM_REG_WRRWEIGHTS_2					 0x16882c
#define QM_REG_WRRWEIGHTS_20					 0x16e010
#define QM_REG_WRRWEIGHTS_21					 0x16e014
#define QM_REG_WRRWEIGHTS_22					 0x16e018
#define QM_REG_WRRWEIGHTS_23					 0x16e01c
#define QM_REG_WRRWEIGHTS_24					 0x16e020
#define QM_REG_WRRWEIGHTS_25					 0x16e024
#define QM_REG_WRRWEIGHTS_26					 0x16e028
#define QM_REG_WRRWEIGHTS_27					 0x16e02c
#define QM_REG_WRRWEIGHTS_28					 0x16e030
#define QM_REG_WRRWEIGHTS_29					 0x16e034
#define QM_REG_WRRWEIGHTS_3					 0x168830
#define QM_REG_WRRWEIGHTS_30					 0x16e038
#define QM_REG_WRRWEIGHTS_31					 0x16e03c
#define QM_REG_WRRWEIGHTS_4					 0x168834
#define QM_REG_WRRWEIGHTS_5					 0x168838
#define QM_REG_WRRWEIGHTS_6					 0x16883c
#define QM_REG_WRRWEIGHTS_7					 0x168840
#define QM_REG_WRRWEIGHTS_8					 0x168844
#define QM_REG_WRRWEIGHTS_9					 0x168848
/* [R 6] Keep the fill level of the fifo from write client 1 */
#define QM_REG_XQM_WRC_FIFOLVL					 0x168000
/* [W 1] reset to parity interrupt */
#define SEM_FAST_REG_PARITY_RST					 0x18840
#define SRC_REG_COUNTFREE0					 0x40500
/* [RW 1] If clr the searcher is compatible to E1 A0 - support only two
   ports. If set the searcher support 8 functions. */
#define SRC_REG_E1HMF_ENABLE					 0x404cc
#define SRC_REG_FIRSTFREE0					 0x40510
#define SRC_REG_KEYRSS0_0					 0x40408
#define SRC_REG_KEYRSS0_7					 0x40424
#define SRC_REG_KEYRSS1_9					 0x40454
#define SRC_REG_KEYSEARCH_0					 0x40458
#define SRC_REG_KEYSEARCH_1					 0x4045c
#define SRC_REG_KEYSEARCH_2					 0x40460
#define SRC_REG_KEYSEARCH_3					 0x40464
#define SRC_REG_KEYSEARCH_4					 0x40468
#define SRC_REG_KEYSEARCH_5					 0x4046c
#define SRC_REG_KEYSEARCH_6					 0x40470
#define SRC_REG_KEYSEARCH_7					 0x40474
#define SRC_REG_KEYSEARCH_8					 0x40478
#define SRC_REG_KEYSEARCH_9					 0x4047c
#define SRC_REG_LASTFREE0					 0x40530
#define SRC_REG_NUMBER_HASH_BITS0				 0x40400
/* [RW 1] Reset internal state machines. */
#define SRC_REG_SOFT_RST					 0x4049c
/* [R 3] Interrupt register #0 read */
#define SRC_REG_SRC_INT_STS					 0x404ac
/* [RW 3] Parity mask register #0 read/write */
#define SRC_REG_SRC_PRTY_MASK					 0x404c8
/* [R 3] Parity register #0 read */
#define SRC_REG_SRC_PRTY_STS					 0x404bc
/* [RC 3] Parity register #0 read clear */
#define SRC_REG_SRC_PRTY_STS_CLR				 0x404c0
/* [R 4] Used to read the value of the XX protection CAM occupancy counter. */
#define TCM_REG_CAM_OCCUP					 0x5017c
/* [RW 1] CDU AG read Interface enable. If 0 - the request input is
   disregarded; valid output is deasserted; all other signals are treated as
   usual; if 1 - normal activity. */
#define TCM_REG_CDU_AG_RD_IFEN					 0x50034
/* [RW 1] CDU AG write Interface enable. If 0 - the request and valid input
   are disregarded; all other signals are treated as usual; if 1 - normal
   activity. */
#define TCM_REG_CDU_AG_WR_IFEN					 0x50030
/* [RW 1] CDU STORM read Interface enable. If 0 - the request input is
   disregarded; valid output is deasserted; all other signals are treated as
   usual; if 1 - normal activity. */
#define TCM_REG_CDU_SM_RD_IFEN					 0x5003c
/* [RW 1] CDU STORM write Interface enable. If 0 - the request and valid
   input is disregarded; all other signals are treated as usual; if 1 -
   normal activity. */
#define TCM_REG_CDU_SM_WR_IFEN					 0x50038
/* [RW 4] CFC output initial credit. Max credit available - 15.Write writes
   the initial credit value; read returns the current value of the credit
   counter. Must be initialized to 1 at start-up. */
#define TCM_REG_CFC_INIT_CRD					 0x50204
/* [RW 3] The weight of the CP input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_CP_WEIGHT					 0x500c0
/* [RW 1] Input csem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define TCM_REG_CSEM_IFEN					 0x5002c
/* [RC 1] Message length mismatch (relative to last indication) at the In#9
   interface. */
#define TCM_REG_CSEM_LENGTH_MIS 				 0x50174
/* [RW 3] The weight of the input csem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_CSEM_WEIGHT					 0x500bc
/* [RW 8] The Event ID in case of ErrorFlg is set in the input message. */
#define TCM_REG_ERR_EVNT_ID					 0x500a0
/* [RW 28] The CM erroneous header for QM and Timers formatting. */
#define TCM_REG_ERR_TCM_HDR					 0x5009c
/* [RW 8] The Event ID for Timers expiration. */
#define TCM_REG_EXPR_EVNT_ID					 0x500a4
/* [RW 8] FIC0 output initial credit. Max credit available - 255.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 64 at start-up. */
#define TCM_REG_FIC0_INIT_CRD					 0x5020c
/* [RW 8] FIC1 output initial credit. Max credit available - 255.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 64 at start-up. */
#define TCM_REG_FIC1_INIT_CRD					 0x50210
/* [RW 1] Arbitration between Input Arbiter groups: 0 - fair Round-Robin; 1
   - strict priority defined by ~tcm_registers_gr_ag_pr.gr_ag_pr;
   ~tcm_registers_gr_ld0_pr.gr_ld0_pr and
   ~tcm_registers_gr_ld1_pr.gr_ld1_pr. */
#define TCM_REG_GR_ARB_TYPE					 0x50114
/* [RW 2] Load (FIC0) channel group priority. The lowest priority is 0; the
   highest priority is 3. It is supposed that the Store channel is the
   compliment of the other 3 groups. */
#define TCM_REG_GR_LD0_PR					 0x5011c
/* [RW 2] Load (FIC1) channel group priority. The lowest priority is 0; the
   highest priority is 3. It is supposed that the Store channel is the
   compliment of the other 3 groups. */
#define TCM_REG_GR_LD1_PR					 0x50120
/* [RW 4] The number of double REG-pairs; loaded from the STORM context and
   sent to STORM; for a specific connection type. The double REG-pairs are
   used to align to STORM context row size of 128 bits. The offset of these
   data in the STORM context is always 0. Index _i stands for the connection
   type (one of 16). */
#define TCM_REG_N_SM_CTX_LD_0					 0x50050
#define TCM_REG_N_SM_CTX_LD_1					 0x50054
#define TCM_REG_N_SM_CTX_LD_2					 0x50058
#define TCM_REG_N_SM_CTX_LD_3					 0x5005c
#define TCM_REG_N_SM_CTX_LD_4					 0x50060
#define TCM_REG_N_SM_CTX_LD_5					 0x50064
/* [RW 1] Input pbf Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define TCM_REG_PBF_IFEN					 0x50024
/* [RC 1] Message length mismatch (relative to last indication) at the In#7
   interface. */
#define TCM_REG_PBF_LENGTH_MIS					 0x5016c
/* [RW 3] The weight of the input pbf in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_PBF_WEIGHT					 0x500b4
#define TCM_REG_PHYS_QNUM0_0					 0x500e0
#define TCM_REG_PHYS_QNUM0_1					 0x500e4
#define TCM_REG_PHYS_QNUM1_0					 0x500e8
#define TCM_REG_PHYS_QNUM1_1					 0x500ec
#define TCM_REG_PHYS_QNUM2_0					 0x500f0
#define TCM_REG_PHYS_QNUM2_1					 0x500f4
#define TCM_REG_PHYS_QNUM3_0					 0x500f8
#define TCM_REG_PHYS_QNUM3_1					 0x500fc
/* [RW 1] Input prs Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define TCM_REG_PRS_IFEN					 0x50020
/* [RC 1] Message length mismatch (relative to last indication) at the In#6
   interface. */
#define TCM_REG_PRS_LENGTH_MIS					 0x50168
/* [RW 3] The weight of the input prs in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_PRS_WEIGHT					 0x500b0
/* [RW 8] The Event ID for Timers formatting in case of stop done. */
#define TCM_REG_STOP_EVNT_ID					 0x500a8
/* [RC 1] Message length mismatch (relative to last indication) at the STORM
   interface. */
#define TCM_REG_STORM_LENGTH_MIS				 0x50160
/* [RW 1] STORM - CM Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define TCM_REG_STORM_TCM_IFEN					 0x50010
/* [RW 3] The weight of the STORM input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_STORM_WEIGHT					 0x500ac
/* [RW 1] CM - CFC Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define TCM_REG_TCM_CFC_IFEN					 0x50040
/* [RW 11] Interrupt mask register #0 read/write */
#define TCM_REG_TCM_INT_MASK					 0x501dc
/* [R 11] Interrupt register #0 read */
#define TCM_REG_TCM_INT_STS					 0x501d0
/* [RW 27] Parity mask register #0 read/write */
#define TCM_REG_TCM_PRTY_MASK					 0x501ec
/* [R 27] Parity register #0 read */
#define TCM_REG_TCM_PRTY_STS					 0x501e0
/* [RC 27] Parity register #0 read clear */
#define TCM_REG_TCM_PRTY_STS_CLR				 0x501e4
/* [RW 3] The size of AG context region 0 in REG-pairs. Designates the MS
   REG-pair number (e.g. if region 0 is 6 REG-pairs; the value should be 5).
   Is used to determine the number of the AG context REG-pairs written back;
   when the input message Reg1WbFlg isn't set. */
#define TCM_REG_TCM_REG0_SZ					 0x500d8
/* [RW 1] CM - STORM 0 Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define TCM_REG_TCM_STORM0_IFEN 				 0x50004
/* [RW 1] CM - STORM 1 Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define TCM_REG_TCM_STORM1_IFEN 				 0x50008
/* [RW 1] CM - QM Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define TCM_REG_TCM_TQM_IFEN					 0x5000c
/* [RW 1] If set the Q index; received from the QM is inserted to event ID. */
#define TCM_REG_TCM_TQM_USE_Q					 0x500d4
/* [RW 28] The CM header for Timers expiration command. */
#define TCM_REG_TM_TCM_HDR					 0x50098
/* [RW 1] Timers - CM Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define TCM_REG_TM_TCM_IFEN					 0x5001c
/* [RW 3] The weight of the Timers input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_TM_WEIGHT					 0x500d0
/* [RW 6] QM output initial credit. Max credit available - 32.Write writes
   the initial credit value; read returns the current value of the credit
   counter. Must be initialized to 32 at start-up. */
#define TCM_REG_TQM_INIT_CRD					 0x5021c
/* [RW 3] The weight of the QM (primary) input in the WRR mechanism. 0
   stands for weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_TQM_P_WEIGHT					 0x500c8
/* [RW 3] The weight of the QM (secondary) input in the WRR mechanism. 0
   stands for weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_TQM_S_WEIGHT					 0x500cc
/* [RW 28] The CM header value for QM request (primary). */
#define TCM_REG_TQM_TCM_HDR_P					 0x50090
/* [RW 28] The CM header value for QM request (secondary). */
#define TCM_REG_TQM_TCM_HDR_S					 0x50094
/* [RW 1] QM - CM Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define TCM_REG_TQM_TCM_IFEN					 0x50014
/* [RW 1] Input SDM Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define TCM_REG_TSDM_IFEN					 0x50018
/* [RC 1] Message length mismatch (relative to last indication) at the SDM
   interface. */
#define TCM_REG_TSDM_LENGTH_MIS 				 0x50164
/* [RW 3] The weight of the SDM input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_TSDM_WEIGHT					 0x500c4
/* [RW 1] Input usem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define TCM_REG_USEM_IFEN					 0x50028
/* [RC 1] Message length mismatch (relative to last indication) at the In#8
   interface. */
#define TCM_REG_USEM_LENGTH_MIS 				 0x50170
/* [RW 3] The weight of the input usem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define TCM_REG_USEM_WEIGHT					 0x500b8
/* [RW 21] Indirect access to the descriptor table of the XX protection
   mechanism. The fields are: [5:0] - length of the message; 15:6] - message
   pointer; 20:16] - next pointer. */
#define TCM_REG_XX_DESCR_TABLE					 0x50280
#define TCM_REG_XX_DESCR_TABLE_SIZE				 29
/* [R 6] Use to read the value of XX protection Free counter. */
#define TCM_REG_XX_FREE 					 0x50178
/* [RW 6] Initial value for the credit counter; responsible for fulfilling
   of the Input Stage XX protection buffer by the XX protection pending
   messages. Max credit available - 127.Write writes the initial credit
   value; read returns the current value of the credit counter. Must be
   initialized to 19 at start-up. */
#define TCM_REG_XX_INIT_CRD					 0x50220
/* [RW 6] Maximum link list size (messages locked) per connection in the XX
   protection. */
#define TCM_REG_XX_MAX_LL_SZ					 0x50044
/* [RW 6] The maximum number of pending messages; which may be stored in XX
   protection. ~tcm_registers_xx_free.xx_free is read on read. */
#define TCM_REG_XX_MSG_NUM					 0x50224
/* [RW 8] The Event ID; sent to the STORM in case of XX overflow. */
#define TCM_REG_XX_OVFL_EVNT_ID 				 0x50048
/* [RW 16] Indirect access to the XX table of the XX protection mechanism.
   The fields are:[4:0] - tail pointer; [10:5] - Link List size; 15:11] -
   header pointer. */
#define TCM_REG_XX_TABLE					 0x50240
/* [RW 4] Load value for cfc ac credit cnt. */
#define TM_REG_CFC_AC_CRDCNT_VAL				 0x164208
/* [RW 4] Load value for cfc cld credit cnt. */
#define TM_REG_CFC_CLD_CRDCNT_VAL				 0x164210
/* [RW 8] Client0 context region. */
#define TM_REG_CL0_CONT_REGION					 0x164030
/* [RW 8] Client1 context region. */
#define TM_REG_CL1_CONT_REGION					 0x164034
/* [RW 8] Client2 context region. */
#define TM_REG_CL2_CONT_REGION					 0x164038
/* [RW 2] Client in High priority client number. */
#define TM_REG_CLIN_PRIOR0_CLIENT				 0x164024
/* [RW 4] Load value for clout0 cred cnt. */
#define TM_REG_CLOUT_CRDCNT0_VAL				 0x164220
/* [RW 4] Load value for clout1 cred cnt. */
#define TM_REG_CLOUT_CRDCNT1_VAL				 0x164228
/* [RW 4] Load value for clout2 cred cnt. */
#define TM_REG_CLOUT_CRDCNT2_VAL				 0x164230
/* [RW 1] Enable client0 input. */
#define TM_REG_EN_CL0_INPUT					 0x164008
/* [RW 1] Enable client1 input. */
#define TM_REG_EN_CL1_INPUT					 0x16400c
/* [RW 1] Enable client2 input. */
#define TM_REG_EN_CL2_INPUT					 0x164010
#define TM_REG_EN_LINEAR0_TIMER 				 0x164014
/* [RW 1] Enable real time counter. */
#define TM_REG_EN_REAL_TIME_CNT 				 0x1640d8
/* [RW 1] Enable for Timers state machines. */
#define TM_REG_EN_TIMERS					 0x164000
/* [RW 4] Load value for expiration credit cnt. CFC max number of
   outstanding load requests for timers (expiration) context loading. */
#define TM_REG_EXP_CRDCNT_VAL					 0x164238
/* [RW 32] Linear0 logic address. */
#define TM_REG_LIN0_LOGIC_ADDR					 0x164240
/* [RW 18] Linear0 Max active cid (in banks of 32 entries). */
#define TM_REG_LIN0_MAX_ACTIVE_CID				 0x164048
/* [ST 16] Linear0 Number of scans counter. */
#define TM_REG_LIN0_NUM_SCANS					 0x1640a0
/* [WB 64] Linear0 phy address. */
#define TM_REG_LIN0_PHY_ADDR					 0x164270
/* [RW 1] Linear0 physical address valid. */
#define TM_REG_LIN0_PHY_ADDR_VALID				 0x164248
#define TM_REG_LIN0_SCAN_ON					 0x1640d0
/* [RW 24] Linear0 array scan timeout. */
#define TM_REG_LIN0_SCAN_TIME					 0x16403c
#define TM_REG_LIN0_VNIC_UC					 0x164128
/* [RW 32] Linear1 logic address. */
#define TM_REG_LIN1_LOGIC_ADDR					 0x164250
/* [WB 64] Linear1 phy address. */
#define TM_REG_LIN1_PHY_ADDR					 0x164280
/* [RW 1] Linear1 physical address valid. */
#define TM_REG_LIN1_PHY_ADDR_VALID				 0x164258
/* [RW 6] Linear timer set_clear fifo threshold. */
#define TM_REG_LIN_SETCLR_FIFO_ALFULL_THR			 0x164070
/* [RW 2] Load value for pci arbiter credit cnt. */
#define TM_REG_PCIARB_CRDCNT_VAL				 0x164260
/* [RW 20] The amount of hardware cycles for each timer tick. */
#define TM_REG_TIMER_TICK_SIZE					 0x16401c
/* [RW 8] Timers Context region. */
#define TM_REG_TM_CONTEXT_REGION				 0x164044
/* [RW 1] Interrupt mask register #0 read/write */
#define TM_REG_TM_INT_MASK					 0x1640fc
/* [R 1] Interrupt register #0 read */
#define TM_REG_TM_INT_STS					 0x1640f0
/* [RW 7] Parity mask register #0 read/write */
#define TM_REG_TM_PRTY_MASK					 0x16410c
/* [RC 7] Parity register #0 read clear */
#define TM_REG_TM_PRTY_STS_CLR					 0x164104
/* [RW 8] The event id for aggregated interrupt 0 */
#define TSDM_REG_AGG_INT_EVENT_0				 0x42038
#define TSDM_REG_AGG_INT_EVENT_1				 0x4203c
#define TSDM_REG_AGG_INT_EVENT_2				 0x42040
#define TSDM_REG_AGG_INT_EVENT_3				 0x42044
#define TSDM_REG_AGG_INT_EVENT_4				 0x42048
/* [RW 1] The T bit for aggregated interrupt 0 */
#define TSDM_REG_AGG_INT_T_0					 0x420b8
#define TSDM_REG_AGG_INT_T_1					 0x420bc
/* [RW 13] The start address in the internal RAM for the cfc_rsp lcid */
#define TSDM_REG_CFC_RSP_START_ADDR				 0x42008
/* [RW 16] The maximum value of the completion counter #0 */
#define TSDM_REG_CMP_COUNTER_MAX0				 0x4201c
/* [RW 16] The maximum value of the completion counter #1 */
#define TSDM_REG_CMP_COUNTER_MAX1				 0x42020
/* [RW 16] The maximum value of the completion counter #2 */
#define TSDM_REG_CMP_COUNTER_MAX2				 0x42024
/* [RW 16] The maximum value of the completion counter #3 */
#define TSDM_REG_CMP_COUNTER_MAX3				 0x42028
/* [RW 13] The start address in the internal RAM for the completion
   counters. */
#define TSDM_REG_CMP_COUNTER_START_ADDR 			 0x4200c
#define TSDM_REG_ENABLE_IN1					 0x42238
#define TSDM_REG_ENABLE_IN2					 0x4223c
#define TSDM_REG_ENABLE_OUT1					 0x42240
#define TSDM_REG_ENABLE_OUT2					 0x42244
/* [RW 4] The initial number of messages that can be sent to the pxp control
   interface without receiving any ACK. */
#define TSDM_REG_INIT_CREDIT_PXP_CTRL				 0x424bc
/* [ST 32] The number of ACK after placement messages received */
#define TSDM_REG_NUM_OF_ACK_AFTER_PLACE 			 0x4227c
/* [ST 32] The number of packet end messages received from the parser */
#define TSDM_REG_NUM_OF_PKT_END_MSG				 0x42274
/* [ST 32] The number of requests received from the pxp async if */
#define TSDM_REG_NUM_OF_PXP_ASYNC_REQ				 0x42278
/* [ST 32] The number of commands received in queue 0 */
#define TSDM_REG_NUM_OF_Q0_CMD					 0x42248
/* [ST 32] The number of commands received in queue 10 */
#define TSDM_REG_NUM_OF_Q10_CMD 				 0x4226c
/* [ST 32] The number of commands received in queue 11 */
#define TSDM_REG_NUM_OF_Q11_CMD 				 0x42270
/* [ST 32] The number of commands received in queue 1 */
#define TSDM_REG_NUM_OF_Q1_CMD					 0x4224c
/* [ST 32] The number of commands received in queue 3 */
#define TSDM_REG_NUM_OF_Q3_CMD					 0x42250
/* [ST 32] The number of commands received in queue 4 */
#define TSDM_REG_NUM_OF_Q4_CMD					 0x42254
/* [ST 32] The number of commands received in queue 5 */
#define TSDM_REG_NUM_OF_Q5_CMD					 0x42258
/* [ST 32] The number of commands received in queue 6 */
#define TSDM_REG_NUM_OF_Q6_CMD					 0x4225c
/* [ST 32] The number of commands received in queue 7 */
#define TSDM_REG_NUM_OF_Q7_CMD					 0x42260
/* [ST 32] The number of commands received in queue 8 */
#define TSDM_REG_NUM_OF_Q8_CMD					 0x42264
/* [ST 32] The number of commands received in queue 9 */
#define TSDM_REG_NUM_OF_Q9_CMD					 0x42268
/* [RW 13] The start address in the internal RAM for the packet end message */
#define TSDM_REG_PCK_END_MSG_START_ADDR 			 0x42014
/* [RW 13] The start address in the internal RAM for queue counters */
#define TSDM_REG_Q_COUNTER_START_ADDR				 0x42010
/* [R 1] pxp_ctrl rd_data fifo empty in sdm_dma_rsp block */
#define TSDM_REG_RSP_PXP_CTRL_RDATA_EMPTY			 0x42548
/* [R 1] parser fifo empty in sdm_sync block */
#define TSDM_REG_SYNC_PARSER_EMPTY				 0x42550
/* [R 1] parser serial fifo empty in sdm_sync block */
#define TSDM_REG_SYNC_SYNC_EMPTY				 0x42558
/* [RW 32] Tick for timer counter. Applicable only when
   ~tsdm_registers_timer_tick_enable.timer_tick_enable =1 */
#define TSDM_REG_TIMER_TICK					 0x42000
/* [RW 32] Interrupt mask register #0 read/write */
#define TSDM_REG_TSDM_INT_MASK_0				 0x4229c
#define TSDM_REG_TSDM_INT_MASK_1				 0x422ac
/* [R 32] Interrupt register #0 read */
#define TSDM_REG_TSDM_INT_STS_0 				 0x42290
#define TSDM_REG_TSDM_INT_STS_1 				 0x422a0
/* [RW 11] Parity mask register #0 read/write */
#define TSDM_REG_TSDM_PRTY_MASK 				 0x422bc
/* [R 11] Parity register #0 read */
#define TSDM_REG_TSDM_PRTY_STS					 0x422b0
/* [RC 11] Parity register #0 read clear */
#define TSDM_REG_TSDM_PRTY_STS_CLR				 0x422b4
/* [RW 5] The number of time_slots in the arbitration cycle */
#define TSEM_REG_ARB_CYCLE_SIZE 				 0x180034
/* [RW 3] The source that is associated with arbitration element 0. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2 */
#define TSEM_REG_ARB_ELEMENT0					 0x180020
/* [RW 3] The source that is associated with arbitration element 1. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~tsem_registers_arb_element0.arb_element0 */
#define TSEM_REG_ARB_ELEMENT1					 0x180024
/* [RW 3] The source that is associated with arbitration element 2. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~tsem_registers_arb_element0.arb_element0
   and ~tsem_registers_arb_element1.arb_element1 */
#define TSEM_REG_ARB_ELEMENT2					 0x180028
/* [RW 3] The source that is associated with arbitration element 3. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.Could
   not be equal to register ~tsem_registers_arb_element0.arb_element0 and
   ~tsem_registers_arb_element1.arb_element1 and
   ~tsem_registers_arb_element2.arb_element2 */
#define TSEM_REG_ARB_ELEMENT3					 0x18002c
/* [RW 3] The source that is associated with arbitration element 4. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~tsem_registers_arb_element0.arb_element0
   and ~tsem_registers_arb_element1.arb_element1 and
   ~tsem_registers_arb_element2.arb_element2 and
   ~tsem_registers_arb_element3.arb_element3 */
#define TSEM_REG_ARB_ELEMENT4					 0x180030
#define TSEM_REG_ENABLE_IN					 0x1800a4
#define TSEM_REG_ENABLE_OUT					 0x1800a8
/* [RW 32] This address space contains all registers and memories that are
   placed in SEM_FAST block. The SEM_FAST registers are described in
   appendix B. In order to access the sem_fast registers the base address
   ~fast_memory.fast_memory should be added to eachsem_fast register offset. */
#define TSEM_REG_FAST_MEMORY					 0x1a0000
/* [RW 1] Disables input messages from FIC0 May be updated during run_time
   by the microcode */
#define TSEM_REG_FIC0_DISABLE					 0x180224
/* [RW 1] Disables input messages from FIC1 May be updated during run_time
   by the microcode */
#define TSEM_REG_FIC1_DISABLE					 0x180234
/* [RW 15] Interrupt table Read and write access to it is not possible in
   the middle of the work */
#define TSEM_REG_INT_TABLE					 0x180400
/* [ST 24] Statistics register. The number of messages that entered through
   FIC0 */
#define TSEM_REG_MSG_NUM_FIC0					 0x180000
/* [ST 24] Statistics register. The number of messages that entered through
   FIC1 */
#define TSEM_REG_MSG_NUM_FIC1					 0x180004
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC0 */
#define TSEM_REG_MSG_NUM_FOC0					 0x180008
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC1 */
#define TSEM_REG_MSG_NUM_FOC1					 0x18000c
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC2 */
#define TSEM_REG_MSG_NUM_FOC2					 0x180010
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC3 */
#define TSEM_REG_MSG_NUM_FOC3					 0x180014
/* [RW 1] Disables input messages from the passive buffer May be updated
   during run_time by the microcode */
#define TSEM_REG_PAS_DISABLE					 0x18024c
/* [WB 128] Debug only. Passive buffer memory */
#define TSEM_REG_PASSIVE_BUFFER 				 0x181000
/* [WB 46] pram memory. B45 is parity; b[44:0] - data. */
#define TSEM_REG_PRAM						 0x1c0000
/* [R 8] Valid sleeping threads indication have bit per thread */
#define TSEM_REG_SLEEP_THREADS_VALID				 0x18026c
/* [R 1] EXT_STORE FIFO is empty in sem_slow_ls_ext */
#define TSEM_REG_SLOW_EXT_STORE_EMPTY				 0x1802a0
/* [RW 8] List of free threads . There is a bit per thread. */
#define TSEM_REG_THREADS_LIST					 0x1802e4
/* [RC 32] Parity register #0 read clear */
#define TSEM_REG_TSEM_PRTY_STS_CLR_0				 0x180118
#define TSEM_REG_TSEM_PRTY_STS_CLR_1				 0x180128
/* [RW 3] The arbitration scheme of time_slot 0 */
#define TSEM_REG_TS_0_AS					 0x180038
/* [RW 3] The arbitration scheme of time_slot 10 */
#define TSEM_REG_TS_10_AS					 0x180060
/* [RW 3] The arbitration scheme of time_slot 11 */
#define TSEM_REG_TS_11_AS					 0x180064
/* [RW 3] The arbitration scheme of time_slot 12 */
#define TSEM_REG_TS_12_AS					 0x180068
/* [RW 3] The arbitration scheme of time_slot 13 */
#define TSEM_REG_TS_13_AS					 0x18006c
/* [RW 3] The arbitration scheme of time_slot 14 */
#define TSEM_REG_TS_14_AS					 0x180070
/* [RW 3] The arbitration scheme of time_slot 15 */
#define TSEM_REG_TS_15_AS					 0x180074
/* [RW 3] The arbitration scheme of time_slot 16 */
#define TSEM_REG_TS_16_AS					 0x180078
/* [RW 3] The arbitration scheme of time_slot 17 */
#define TSEM_REG_TS_17_AS					 0x18007c
/* [RW 3] The arbitration scheme of time_slot 18 */
#define TSEM_REG_TS_18_AS					 0x180080
/* [RW 3] The arbitration scheme of time_slot 1 */
#define TSEM_REG_TS_1_AS					 0x18003c
/* [RW 3] The arbitration scheme of time_slot 2 */
#define TSEM_REG_TS_2_AS					 0x180040
/* [RW 3] The arbitration scheme of time_slot 3 */
#define TSEM_REG_TS_3_AS					 0x180044
/* [RW 3] The arbitration scheme of time_slot 4 */
#define TSEM_REG_TS_4_AS					 0x180048
/* [RW 3] The arbitration scheme of time_slot 5 */
#define TSEM_REG_TS_5_AS					 0x18004c
/* [RW 3] The arbitration scheme of time_slot 6 */
#define TSEM_REG_TS_6_AS					 0x180050
/* [RW 3] The arbitration scheme of time_slot 7 */
#define TSEM_REG_TS_7_AS					 0x180054
/* [RW 3] The arbitration scheme of time_slot 8 */
#define TSEM_REG_TS_8_AS					 0x180058
/* [RW 3] The arbitration scheme of time_slot 9 */
#define TSEM_REG_TS_9_AS					 0x18005c
/* [RW 32] Interrupt mask register #0 read/write */
#define TSEM_REG_TSEM_INT_MASK_0				 0x180100
#define TSEM_REG_TSEM_INT_MASK_1				 0x180110
/* [R 32] Interrupt register #0 read */
#define TSEM_REG_TSEM_INT_STS_0 				 0x1800f4
#define TSEM_REG_TSEM_INT_STS_1 				 0x180104
/* [RW 32] Parity mask register #0 read/write */
#define TSEM_REG_TSEM_PRTY_MASK_0				 0x180120
#define TSEM_REG_TSEM_PRTY_MASK_1				 0x180130
/* [R 32] Parity register #0 read */
#define TSEM_REG_TSEM_PRTY_STS_0				 0x180114
#define TSEM_REG_TSEM_PRTY_STS_1				 0x180124
/* [W 7] VF or PF ID for reset error bit. Values 0-63 reset error bit for 64
 * VF; values 64-67 reset error for 4 PF; values 68-127 are not valid. */
#define TSEM_REG_VFPF_ERR_NUM					 0x180380
/* [RW 32] Indirect access to AG context with 32-bits granularity. The bits
 * [10:8] of the address should be the offset within the accessed LCID
 * context; the bits [7:0] are the accessed LCID.Example: to write to REG10
 * LCID100. The RBC address should be 12'ha64. */
#define UCM_REG_AG_CTX						 0xe2000
/* [R 5] Used to read the XX protection CAM occupancy counter. */
#define UCM_REG_CAM_OCCUP					 0xe0170
/* [RW 1] CDU AG read Interface enable. If 0 - the request input is
   disregarded; valid output is deasserted; all other signals are treated as
   usual; if 1 - normal activity. */
#define UCM_REG_CDU_AG_RD_IFEN					 0xe0038
/* [RW 1] CDU AG write Interface enable. If 0 - the request and valid input
   are disregarded; all other signals are treated as usual; if 1 - normal
   activity. */
#define UCM_REG_CDU_AG_WR_IFEN					 0xe0034
/* [RW 1] CDU STORM read Interface enable. If 0 - the request input is
   disregarded; valid output is deasserted; all other signals are treated as
   usual; if 1 - normal activity. */
#define UCM_REG_CDU_SM_RD_IFEN					 0xe0040
/* [RW 1] CDU STORM write Interface enable. If 0 - the request and valid
   input is disregarded; all other signals are treated as usual; if 1 -
   normal activity. */
#define UCM_REG_CDU_SM_WR_IFEN					 0xe003c
/* [RW 4] CFC output initial credit. Max credit available - 15.Write writes
   the initial credit value; read returns the current value of the credit
   counter. Must be initialized to 1 at start-up. */
#define UCM_REG_CFC_INIT_CRD					 0xe0204
/* [RW 3] The weight of the CP input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_CP_WEIGHT					 0xe00c4
/* [RW 1] Input csem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define UCM_REG_CSEM_IFEN					 0xe0028
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the csem interface is detected. */
#define UCM_REG_CSEM_LENGTH_MIS 				 0xe0160
/* [RW 3] The weight of the input csem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_CSEM_WEIGHT					 0xe00b8
/* [RW 1] Input dorq Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define UCM_REG_DORQ_IFEN					 0xe0030
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the dorq interface is detected. */
#define UCM_REG_DORQ_LENGTH_MIS 				 0xe0168
/* [RW 3] The weight of the input dorq in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_DORQ_WEIGHT					 0xe00c0
/* [RW 8] The Event ID in case ErrorFlg input message bit is set. */
#define UCM_REG_ERR_EVNT_ID					 0xe00a4
/* [RW 28] The CM erroneous header for QM and Timers formatting. */
#define UCM_REG_ERR_UCM_HDR					 0xe00a0
/* [RW 8] The Event ID for Timers expiration. */
#define UCM_REG_EXPR_EVNT_ID					 0xe00a8
/* [RW 8] FIC0 output initial credit. Max credit available - 255.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 64 at start-up. */
#define UCM_REG_FIC0_INIT_CRD					 0xe020c
/* [RW 8] FIC1 output initial credit. Max credit available - 255.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 64 at start-up. */
#define UCM_REG_FIC1_INIT_CRD					 0xe0210
/* [RW 1] Arbitration between Input Arbiter groups: 0 - fair Round-Robin; 1
   - strict priority defined by ~ucm_registers_gr_ag_pr.gr_ag_pr;
   ~ucm_registers_gr_ld0_pr.gr_ld0_pr and
   ~ucm_registers_gr_ld1_pr.gr_ld1_pr. */
#define UCM_REG_GR_ARB_TYPE					 0xe0144
/* [RW 2] Load (FIC0) channel group priority. The lowest priority is 0; the
   highest priority is 3. It is supposed that the Store channel group is
   compliment to the others. */
#define UCM_REG_GR_LD0_PR					 0xe014c
/* [RW 2] Load (FIC1) channel group priority. The lowest priority is 0; the
   highest priority is 3. It is supposed that the Store channel group is
   compliment to the others. */
#define UCM_REG_GR_LD1_PR					 0xe0150
/* [RW 2] The queue index for invalidate counter flag decision. */
#define UCM_REG_INV_CFLG_Q					 0xe00e4
/* [RW 5] The number of double REG-pairs; loaded from the STORM context and
   sent to STORM; for a specific connection type. the double REG-pairs are
   used in order to align to STORM context row size of 128 bits. The offset
   of these data in the STORM context is always 0. Index _i stands for the
   connection type (one of 16). */
#define UCM_REG_N_SM_CTX_LD_0					 0xe0054
#define UCM_REG_N_SM_CTX_LD_1					 0xe0058
#define UCM_REG_N_SM_CTX_LD_2					 0xe005c
#define UCM_REG_N_SM_CTX_LD_3					 0xe0060
#define UCM_REG_N_SM_CTX_LD_4					 0xe0064
#define UCM_REG_N_SM_CTX_LD_5					 0xe0068
#define UCM_REG_PHYS_QNUM0_0					 0xe0110
#define UCM_REG_PHYS_QNUM0_1					 0xe0114
#define UCM_REG_PHYS_QNUM1_0					 0xe0118
#define UCM_REG_PHYS_QNUM1_1					 0xe011c
#define UCM_REG_PHYS_QNUM2_0					 0xe0120
#define UCM_REG_PHYS_QNUM2_1					 0xe0124
#define UCM_REG_PHYS_QNUM3_0					 0xe0128
#define UCM_REG_PHYS_QNUM3_1					 0xe012c
/* [RW 8] The Event ID for Timers formatting in case of stop done. */
#define UCM_REG_STOP_EVNT_ID					 0xe00ac
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the STORM interface is detected. */
#define UCM_REG_STORM_LENGTH_MIS				 0xe0154
/* [RW 1] STORM - CM Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define UCM_REG_STORM_UCM_IFEN					 0xe0010
/* [RW 3] The weight of the STORM input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_STORM_WEIGHT					 0xe00b0
/* [RW 4] Timers output initial credit. Max credit available - 15.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 4 at start-up. */
#define UCM_REG_TM_INIT_CRD					 0xe021c
/* [RW 28] The CM header for Timers expiration command. */
#define UCM_REG_TM_UCM_HDR					 0xe009c
/* [RW 1] Timers - CM Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define UCM_REG_TM_UCM_IFEN					 0xe001c
/* [RW 3] The weight of the Timers input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_TM_WEIGHT					 0xe00d4
/* [RW 1] Input tsem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define UCM_REG_TSEM_IFEN					 0xe0024
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the tsem interface is detected. */
#define UCM_REG_TSEM_LENGTH_MIS 				 0xe015c
/* [RW 3] The weight of the input tsem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_TSEM_WEIGHT					 0xe00b4
/* [RW 1] CM - CFC Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define UCM_REG_UCM_CFC_IFEN					 0xe0044
/* [RW 11] Interrupt mask register #0 read/write */
#define UCM_REG_UCM_INT_MASK					 0xe01d4
/* [R 11] Interrupt register #0 read */
#define UCM_REG_UCM_INT_STS					 0xe01c8
/* [RW 27] Parity mask register #0 read/write */
#define UCM_REG_UCM_PRTY_MASK					 0xe01e4
/* [R 27] Parity register #0 read */
#define UCM_REG_UCM_PRTY_STS					 0xe01d8
/* [RC 27] Parity register #0 read clear */
#define UCM_REG_UCM_PRTY_STS_CLR				 0xe01dc
/* [RW 2] The size of AG context region 0 in REG-pairs. Designates the MS
   REG-pair number (e.g. if region 0 is 6 REG-pairs; the value should be 5).
   Is used to determine the number of the AG context REG-pairs written back;
   when the Reg1WbFlg isn't set. */
#define UCM_REG_UCM_REG0_SZ					 0xe00dc
/* [RW 1] CM - STORM 0 Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define UCM_REG_UCM_STORM0_IFEN 				 0xe0004
/* [RW 1] CM - STORM 1 Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define UCM_REG_UCM_STORM1_IFEN 				 0xe0008
/* [RW 1] CM - Timers Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define UCM_REG_UCM_TM_IFEN					 0xe0020
/* [RW 1] CM - QM Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define UCM_REG_UCM_UQM_IFEN					 0xe000c
/* [RW 1] If set the Q index; received from the QM is inserted to event ID. */
#define UCM_REG_UCM_UQM_USE_Q					 0xe00d8
/* [RW 6] QM output initial credit. Max credit available - 32.Write writes
   the initial credit value; read returns the current value of the credit
   counter. Must be initialized to 32 at start-up. */
#define UCM_REG_UQM_INIT_CRD					 0xe0220
/* [RW 3] The weight of the QM (primary) input in the WRR mechanism. 0
   stands for weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_UQM_P_WEIGHT					 0xe00cc
/* [RW 3] The weight of the QM (secondary) input in the WRR mechanism. 0
   stands for weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_UQM_S_WEIGHT					 0xe00d0
/* [RW 28] The CM header value for QM request (primary). */
#define UCM_REG_UQM_UCM_HDR_P					 0xe0094
/* [RW 28] The CM header value for QM request (secondary). */
#define UCM_REG_UQM_UCM_HDR_S					 0xe0098
/* [RW 1] QM - CM Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define UCM_REG_UQM_UCM_IFEN					 0xe0014
/* [RW 1] Input SDM Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define UCM_REG_USDM_IFEN					 0xe0018
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the SDM interface is detected. */
#define UCM_REG_USDM_LENGTH_MIS 				 0xe0158
/* [RW 3] The weight of the SDM input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_USDM_WEIGHT					 0xe00c8
/* [RW 1] Input xsem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define UCM_REG_XSEM_IFEN					 0xe002c
/* [RC 1] Set when the message length mismatch (relative to last indication)
   at the xsem interface isdetected. */
#define UCM_REG_XSEM_LENGTH_MIS 				 0xe0164
/* [RW 3] The weight of the input xsem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define UCM_REG_XSEM_WEIGHT					 0xe00bc
/* [RW 20] Indirect access to the descriptor table of the XX protection
   mechanism. The fields are:[5:0] - message length; 14:6] - message
   pointer; 19:15] - next pointer. */
#define UCM_REG_XX_DESCR_TABLE					 0xe0280
#define UCM_REG_XX_DESCR_TABLE_SIZE				 27
/* [R 6] Use to read the XX protection Free counter. */
#define UCM_REG_XX_FREE 					 0xe016c
/* [RW 6] Initial value for the credit counter; responsible for fulfilling
   of the Input Stage XX protection buffer by the XX protection pending
   messages. Write writes the initial credit value; read returns the current
   value of the credit counter. Must be initialized to 12 at start-up. */
#define UCM_REG_XX_INIT_CRD					 0xe0224
/* [RW 6] The maximum number of pending messages; which may be stored in XX
   protection. ~ucm_registers_xx_free.xx_free read on read. */
#define UCM_REG_XX_MSG_NUM					 0xe0228
/* [RW 8] The Event ID; sent to the STORM in case of XX overflow. */
#define UCM_REG_XX_OVFL_EVNT_ID 				 0xe004c
/* [RW 16] Indirect access to the XX table of the XX protection mechanism.
   The fields are: [4:0] - tail pointer; 10:5] - Link List size; 15:11] -
   header pointer. */
#define UCM_REG_XX_TABLE					 0xe0300
#define UMAC_COMMAND_CONFIG_REG_HD_ENA				 (0x1<<10)
#define UMAC_COMMAND_CONFIG_REG_IGNORE_TX_PAUSE			 (0x1<<28)
#define UMAC_COMMAND_CONFIG_REG_LOOP_ENA			 (0x1<<15)
#define UMAC_COMMAND_CONFIG_REG_NO_LGTH_CHECK			 (0x1<<24)
#define UMAC_COMMAND_CONFIG_REG_PAD_EN				 (0x1<<5)
#define UMAC_COMMAND_CONFIG_REG_PAUSE_IGNORE			 (0x1<<8)
#define UMAC_COMMAND_CONFIG_REG_PROMIS_EN			 (0x1<<4)
#define UMAC_COMMAND_CONFIG_REG_RX_ENA				 (0x1<<1)
#define UMAC_COMMAND_CONFIG_REG_SW_RESET			 (0x1<<13)
#define UMAC_COMMAND_CONFIG_REG_TX_ENA				 (0x1<<0)
#define UMAC_REG_COMMAND_CONFIG					 0x8
/* [RW 32] Register Bit 0 refers to Bit 16 of the MAC address; Bit 1 refers
 * to bit 17 of the MAC address etc. */
#define UMAC_REG_MAC_ADDR0					 0xc
/* [RW 16] Register Bit 0 refers to Bit 0 of the MAC address; Register Bit 1
 * refers to Bit 1 of the MAC address etc. Bits 16 to 31 are reserved. */
#define UMAC_REG_MAC_ADDR1					 0x10
/* [RW 14] Defines a 14-Bit maximum frame length used by the MAC receive
 * logic to check frames. */
#define UMAC_REG_MAXFR						 0x14
/* [RW 8] The event id for aggregated interrupt 0 */
#define USDM_REG_AGG_INT_EVENT_0				 0xc4038
#define USDM_REG_AGG_INT_EVENT_1				 0xc403c
#define USDM_REG_AGG_INT_EVENT_2				 0xc4040
#define USDM_REG_AGG_INT_EVENT_4				 0xc4048
#define USDM_REG_AGG_INT_EVENT_5				 0xc404c
#define USDM_REG_AGG_INT_EVENT_6				 0xc4050
/* [RW 1] For each aggregated interrupt index whether the mode is normal (0)
   or auto-mask-mode (1) */
#define USDM_REG_AGG_INT_MODE_0 				 0xc41b8
#define USDM_REG_AGG_INT_MODE_1 				 0xc41bc
#define USDM_REG_AGG_INT_MODE_4 				 0xc41c8
#define USDM_REG_AGG_INT_MODE_5 				 0xc41cc
#define USDM_REG_AGG_INT_MODE_6 				 0xc41d0
/* [RW 1] The T bit for aggregated interrupt 5 */
#define USDM_REG_AGG_INT_T_5					 0xc40cc
#define USDM_REG_AGG_INT_T_6					 0xc40d0
/* [RW 13] The start address in the internal RAM for the cfc_rsp lcid */
#define USDM_REG_CFC_RSP_START_ADDR				 0xc4008
/* [RW 16] The maximum value of the completion counter #0 */
#define USDM_REG_CMP_COUNTER_MAX0				 0xc401c
/* [RW 16] The maximum value of the completion counter #1 */
#define USDM_REG_CMP_COUNTER_MAX1				 0xc4020
/* [RW 16] The maximum value of the completion counter #2 */
#define USDM_REG_CMP_COUNTER_MAX2				 0xc4024
/* [RW 16] The maximum value of the completion counter #3 */
#define USDM_REG_CMP_COUNTER_MAX3				 0xc4028
/* [RW 13] The start address in the internal RAM for the completion
   counters. */
#define USDM_REG_CMP_COUNTER_START_ADDR 			 0xc400c
#define USDM_REG_ENABLE_IN1					 0xc4238
#define USDM_REG_ENABLE_IN2					 0xc423c
#define USDM_REG_ENABLE_OUT1					 0xc4240
#define USDM_REG_ENABLE_OUT2					 0xc4244
/* [RW 4] The initial number of messages that can be sent to the pxp control
   interface without receiving any ACK. */
#define USDM_REG_INIT_CREDIT_PXP_CTRL				 0xc44c0
/* [ST 32] The number of ACK after placement messages received */
#define USDM_REG_NUM_OF_ACK_AFTER_PLACE 			 0xc4280
/* [ST 32] The number of packet end messages received from the parser */
#define USDM_REG_NUM_OF_PKT_END_MSG				 0xc4278
/* [ST 32] The number of requests received from the pxp async if */
#define USDM_REG_NUM_OF_PXP_ASYNC_REQ				 0xc427c
/* [ST 32] The number of commands received in queue 0 */
#define USDM_REG_NUM_OF_Q0_CMD					 0xc4248
/* [ST 32] The number of commands received in queue 10 */
#define USDM_REG_NUM_OF_Q10_CMD 				 0xc4270
/* [ST 32] The number of commands received in queue 11 */
#define USDM_REG_NUM_OF_Q11_CMD 				 0xc4274
/* [ST 32] The number of commands received in queue 1 */
#define USDM_REG_NUM_OF_Q1_CMD					 0xc424c
/* [ST 32] The number of commands received in queue 2 */
#define USDM_REG_NUM_OF_Q2_CMD					 0xc4250
/* [ST 32] The number of commands received in queue 3 */
#define USDM_REG_NUM_OF_Q3_CMD					 0xc4254
/* [ST 32] The number of commands received in queue 4 */
#define USDM_REG_NUM_OF_Q4_CMD					 0xc4258
/* [ST 32] The number of commands received in queue 5 */
#define USDM_REG_NUM_OF_Q5_CMD					 0xc425c
/* [ST 32] The number of commands received in queue 6 */
#define USDM_REG_NUM_OF_Q6_CMD					 0xc4260
/* [ST 32] The number of commands received in queue 7 */
#define USDM_REG_NUM_OF_Q7_CMD					 0xc4264
/* [ST 32] The number of commands received in queue 8 */
#define USDM_REG_NUM_OF_Q8_CMD					 0xc4268
/* [ST 32] The number of commands received in queue 9 */
#define USDM_REG_NUM_OF_Q9_CMD					 0xc426c
/* [RW 13] The start address in the internal RAM for the packet end message */
#define USDM_REG_PCK_END_MSG_START_ADDR 			 0xc4014
/* [RW 13] The start address in the internal RAM for queue counters */
#define USDM_REG_Q_COUNTER_START_ADDR				 0xc4010
/* [R 1] pxp_ctrl rd_data fifo empty in sdm_dma_rsp block */
#define USDM_REG_RSP_PXP_CTRL_RDATA_EMPTY			 0xc4550
/* [R 1] parser fifo empty in sdm_sync block */
#define USDM_REG_SYNC_PARSER_EMPTY				 0xc4558
/* [R 1] parser serial fifo empty in sdm_sync block */
#define USDM_REG_SYNC_SYNC_EMPTY				 0xc4560
/* [RW 32] Tick for timer counter. Applicable only when
   ~usdm_registers_timer_tick_enable.timer_tick_enable =1 */
#define USDM_REG_TIMER_TICK					 0xc4000
/* [RW 32] Interrupt mask register #0 read/write */
#define USDM_REG_USDM_INT_MASK_0				 0xc42a0
#define USDM_REG_USDM_INT_MASK_1				 0xc42b0
/* [R 32] Interrupt register #0 read */
#define USDM_REG_USDM_INT_STS_0 				 0xc4294
#define USDM_REG_USDM_INT_STS_1 				 0xc42a4
/* [RW 11] Parity mask register #0 read/write */
#define USDM_REG_USDM_PRTY_MASK 				 0xc42c0
/* [R 11] Parity register #0 read */
#define USDM_REG_USDM_PRTY_STS					 0xc42b4
/* [RC 11] Parity register #0 read clear */
#define USDM_REG_USDM_PRTY_STS_CLR				 0xc42b8
/* [RW 5] The number of time_slots in the arbitration cycle */
#define USEM_REG_ARB_CYCLE_SIZE 				 0x300034
/* [RW 3] The source that is associated with arbitration element 0. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2 */
#define USEM_REG_ARB_ELEMENT0					 0x300020
/* [RW 3] The source that is associated with arbitration element 1. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~usem_registers_arb_element0.arb_element0 */
#define USEM_REG_ARB_ELEMENT1					 0x300024
/* [RW 3] The source that is associated with arbitration element 2. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~usem_registers_arb_element0.arb_element0
   and ~usem_registers_arb_element1.arb_element1 */
#define USEM_REG_ARB_ELEMENT2					 0x300028
/* [RW 3] The source that is associated with arbitration element 3. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.Could
   not be equal to register ~usem_registers_arb_element0.arb_element0 and
   ~usem_registers_arb_element1.arb_element1 and
   ~usem_registers_arb_element2.arb_element2 */
#define USEM_REG_ARB_ELEMENT3					 0x30002c
/* [RW 3] The source that is associated with arbitration element 4. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~usem_registers_arb_element0.arb_element0
   and ~usem_registers_arb_element1.arb_element1 and
   ~usem_registers_arb_element2.arb_element2 and
   ~usem_registers_arb_element3.arb_element3 */
#define USEM_REG_ARB_ELEMENT4					 0x300030
#define USEM_REG_ENABLE_IN					 0x3000a4
#define USEM_REG_ENABLE_OUT					 0x3000a8
/* [RW 32] This address space contains all registers and memories that are
   placed in SEM_FAST block. The SEM_FAST registers are described in
   appendix B. In order to access the sem_fast registers the base address
   ~fast_memory.fast_memory should be added to eachsem_fast register offset. */
#define USEM_REG_FAST_MEMORY					 0x320000
/* [RW 1] Disables input messages from FIC0 May be updated during run_time
   by the microcode */
#define USEM_REG_FIC0_DISABLE					 0x300224
/* [RW 1] Disables input messages from FIC1 May be updated during run_time
   by the microcode */
#define USEM_REG_FIC1_DISABLE					 0x300234
/* [RW 15] Interrupt table Read and write access to it is not possible in
   the middle of the work */
#define USEM_REG_INT_TABLE					 0x300400
/* [ST 24] Statistics register. The number of messages that entered through
   FIC0 */
#define USEM_REG_MSG_NUM_FIC0					 0x300000
/* [ST 24] Statistics register. The number of messages that entered through
   FIC1 */
#define USEM_REG_MSG_NUM_FIC1					 0x300004
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC0 */
#define USEM_REG_MSG_NUM_FOC0					 0x300008
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC1 */
#define USEM_REG_MSG_NUM_FOC1					 0x30000c
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC2 */
#define USEM_REG_MSG_NUM_FOC2					 0x300010
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC3 */
#define USEM_REG_MSG_NUM_FOC3					 0x300014
/* [RW 1] Disables input messages from the passive buffer May be updated
   during run_time by the microcode */
#define USEM_REG_PAS_DISABLE					 0x30024c
/* [WB 128] Debug only. Passive buffer memory */
#define USEM_REG_PASSIVE_BUFFER 				 0x302000
/* [WB 46] pram memory. B45 is parity; b[44:0] - data. */
#define USEM_REG_PRAM						 0x340000
/* [R 16] Valid sleeping threads indication have bit per thread */
#define USEM_REG_SLEEP_THREADS_VALID				 0x30026c
/* [R 1] EXT_STORE FIFO is empty in sem_slow_ls_ext */
#define USEM_REG_SLOW_EXT_STORE_EMPTY				 0x3002a0
/* [RW 16] List of free threads . There is a bit per thread. */
#define USEM_REG_THREADS_LIST					 0x3002e4
/* [RW 3] The arbitration scheme of time_slot 0 */
#define USEM_REG_TS_0_AS					 0x300038
/* [RW 3] The arbitration scheme of time_slot 10 */
#define USEM_REG_TS_10_AS					 0x300060
/* [RW 3] The arbitration scheme of time_slot 11 */
#define USEM_REG_TS_11_AS					 0x300064
/* [RW 3] The arbitration scheme of time_slot 12 */
#define USEM_REG_TS_12_AS					 0x300068
/* [RW 3] The arbitration scheme of time_slot 13 */
#define USEM_REG_TS_13_AS					 0x30006c
/* [RW 3] The arbitration scheme of time_slot 14 */
#define USEM_REG_TS_14_AS					 0x300070
/* [RW 3] The arbitration scheme of time_slot 15 */
#define USEM_REG_TS_15_AS					 0x300074
/* [RW 3] The arbitration scheme of time_slot 16 */
#define USEM_REG_TS_16_AS					 0x300078
/* [RW 3] The arbitration scheme of time_slot 17 */
#define USEM_REG_TS_17_AS					 0x30007c
/* [RW 3] The arbitration scheme of time_slot 18 */
#define USEM_REG_TS_18_AS					 0x300080
/* [RW 3] The arbitration scheme of time_slot 1 */
#define USEM_REG_TS_1_AS					 0x30003c
/* [RW 3] The arbitration scheme of time_slot 2 */
#define USEM_REG_TS_2_AS					 0x300040
/* [RW 3] The arbitration scheme of time_slot 3 */
#define USEM_REG_TS_3_AS					 0x300044
/* [RW 3] The arbitration scheme of time_slot 4 */
#define USEM_REG_TS_4_AS					 0x300048
/* [RW 3] The arbitration scheme of time_slot 5 */
#define USEM_REG_TS_5_AS					 0x30004c
/* [RW 3] The arbitration scheme of time_slot 6 */
#define USEM_REG_TS_6_AS					 0x300050
/* [RW 3] The arbitration scheme of time_slot 7 */
#define USEM_REG_TS_7_AS					 0x300054
/* [RW 3] The arbitration scheme of time_slot 8 */
#define USEM_REG_TS_8_AS					 0x300058
/* [RW 3] The arbitration scheme of time_slot 9 */
#define USEM_REG_TS_9_AS					 0x30005c
/* [RW 32] Interrupt mask register #0 read/write */
#define USEM_REG_USEM_INT_MASK_0				 0x300110
#define USEM_REG_USEM_INT_MASK_1				 0x300120
/* [R 32] Interrupt register #0 read */
#define USEM_REG_USEM_INT_STS_0 				 0x300104
#define USEM_REG_USEM_INT_STS_1 				 0x300114
/* [RW 32] Parity mask register #0 read/write */
#define USEM_REG_USEM_PRTY_MASK_0				 0x300130
#define USEM_REG_USEM_PRTY_MASK_1				 0x300140
/* [R 32] Parity register #0 read */
#define USEM_REG_USEM_PRTY_STS_0				 0x300124
#define USEM_REG_USEM_PRTY_STS_1				 0x300134
/* [RC 32] Parity register #0 read clear */
#define USEM_REG_USEM_PRTY_STS_CLR_0				 0x300128
#define USEM_REG_USEM_PRTY_STS_CLR_1				 0x300138
/* [W 7] VF or PF ID for reset error bit. Values 0-63 reset error bit for 64
 * VF; values 64-67 reset error for 4 PF; values 68-127 are not valid. */
#define USEM_REG_VFPF_ERR_NUM					 0x300380
#define VFC_MEMORIES_RST_REG_CAM_RST				 (0x1<<0)
#define VFC_MEMORIES_RST_REG_RAM_RST				 (0x1<<1)
#define VFC_REG_MEMORIES_RST					 0x1943c
/* [RW 32] Indirect access to AG context with 32-bits granularity. The bits
 * [12:8] of the address should be the offset within the accessed LCID
 * context; the bits [7:0] are the accessed LCID.Example: to write to REG10
 * LCID100. The RBC address should be 13'ha64. */
#define XCM_REG_AG_CTX						 0x28000
/* [RW 2] The queue index for registration on Aux1 counter flag. */
#define XCM_REG_AUX1_Q						 0x20134
/* [RW 2] Per each decision rule the queue index to register to. */
#define XCM_REG_AUX_CNT_FLG_Q_19				 0x201b0
/* [R 5] Used to read the XX protection CAM occupancy counter. */
#define XCM_REG_CAM_OCCUP					 0x20244
/* [RW 1] CDU AG read Interface enable. If 0 - the request input is
   disregarded; valid output is deasserted; all other signals are treated as
   usual; if 1 - normal activity. */
#define XCM_REG_CDU_AG_RD_IFEN					 0x20044
/* [RW 1] CDU AG write Interface enable. If 0 - the request and valid input
   are disregarded; all other signals are treated as usual; if 1 - normal
   activity. */
#define XCM_REG_CDU_AG_WR_IFEN					 0x20040
/* [RW 1] CDU STORM read Interface enable. If 0 - the request input is
   disregarded; valid output is deasserted; all other signals are treated as
   usual; if 1 - normal activity. */
#define XCM_REG_CDU_SM_RD_IFEN					 0x2004c
/* [RW 1] CDU STORM write Interface enable. If 0 - the request and valid
   input is disregarded; all other signals are treated as usual; if 1 -
   normal activity. */
#define XCM_REG_CDU_SM_WR_IFEN					 0x20048
/* [RW 4] CFC output initial credit. Max credit available - 15.Write writes
   the initial credit value; read returns the current value of the credit
   counter. Must be initialized to 1 at start-up. */
#define XCM_REG_CFC_INIT_CRD					 0x20404
/* [RW 3] The weight of the CP input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_CP_WEIGHT					 0x200dc
/* [RW 1] Input csem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_CSEM_IFEN					 0x20028
/* [RC 1] Set at message length mismatch (relative to last indication) at
   the csem interface. */
#define XCM_REG_CSEM_LENGTH_MIS 				 0x20228
/* [RW 3] The weight of the input csem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_CSEM_WEIGHT					 0x200c4
/* [RW 1] Input dorq Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_DORQ_IFEN					 0x20030
/* [RC 1] Set at message length mismatch (relative to last indication) at
   the dorq interface. */
#define XCM_REG_DORQ_LENGTH_MIS 				 0x20230
/* [RW 3] The weight of the input dorq in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_DORQ_WEIGHT					 0x200cc
/* [RW 8] The Event ID in case the ErrorFlg input message bit is set. */
#define XCM_REG_ERR_EVNT_ID					 0x200b0
/* [RW 28] The CM erroneous header for QM and Timers formatting. */
#define XCM_REG_ERR_XCM_HDR					 0x200ac
/* [RW 8] The Event ID for Timers expiration. */
#define XCM_REG_EXPR_EVNT_ID					 0x200b4
/* [RW 8] FIC0 output initial credit. Max credit available - 255.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 64 at start-up. */
#define XCM_REG_FIC0_INIT_CRD					 0x2040c
/* [RW 8] FIC1 output initial credit. Max credit available - 255.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 64 at start-up. */
#define XCM_REG_FIC1_INIT_CRD					 0x20410
#define XCM_REG_GLB_DEL_ACK_MAX_CNT_0				 0x20118
#define XCM_REG_GLB_DEL_ACK_MAX_CNT_1				 0x2011c
#define XCM_REG_GLB_DEL_ACK_TMR_VAL_0				 0x20108
#define XCM_REG_GLB_DEL_ACK_TMR_VAL_1				 0x2010c
/* [RW 1] Arbitratiojn between Input Arbiter groups: 0 - fair Round-Robin; 1
   - strict priority defined by ~xcm_registers_gr_ag_pr.gr_ag_pr;
   ~xcm_registers_gr_ld0_pr.gr_ld0_pr and
   ~xcm_registers_gr_ld1_pr.gr_ld1_pr. */
#define XCM_REG_GR_ARB_TYPE					 0x2020c
/* [RW 2] Load (FIC0) channel group priority. The lowest priority is 0; the
   highest priority is 3. It is supposed that the Channel group is the
   compliment of the other 3 groups. */
#define XCM_REG_GR_LD0_PR					 0x20214
/* [RW 2] Load (FIC1) channel group priority. The lowest priority is 0; the
   highest priority is 3. It is supposed that the Channel group is the
   compliment of the other 3 groups. */
#define XCM_REG_GR_LD1_PR					 0x20218
/* [RW 1] Input nig0 Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_NIG0_IFEN					 0x20038
/* [RC 1] Set at message length mismatch (relative to last indication) at
   the nig0 interface. */
#define XCM_REG_NIG0_LENGTH_MIS 				 0x20238
/* [RW 3] The weight of the input nig0 in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_NIG0_WEIGHT					 0x200d4
/* [RW 1] Input nig1 Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_NIG1_IFEN					 0x2003c
/* [RC 1] Set at message length mismatch (relative to last indication) at
   the nig1 interface. */
#define XCM_REG_NIG1_LENGTH_MIS 				 0x2023c
/* [RW 5] The number of double REG-pairs; loaded from the STORM context and
   sent to STORM; for a specific connection type. The double REG-pairs are
   used in order to align to STORM context row size of 128 bits. The offset
   of these data in the STORM context is always 0. Index _i stands for the
   connection type (one of 16). */
#define XCM_REG_N_SM_CTX_LD_0					 0x20060
#define XCM_REG_N_SM_CTX_LD_1					 0x20064
#define XCM_REG_N_SM_CTX_LD_2					 0x20068
#define XCM_REG_N_SM_CTX_LD_3					 0x2006c
#define XCM_REG_N_SM_CTX_LD_4					 0x20070
#define XCM_REG_N_SM_CTX_LD_5					 0x20074
/* [RW 1] Input pbf Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define XCM_REG_PBF_IFEN					 0x20034
/* [RC 1] Set at message length mismatch (relative to last indication) at
   the pbf interface. */
#define XCM_REG_PBF_LENGTH_MIS					 0x20234
/* [RW 3] The weight of the input pbf in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_PBF_WEIGHT					 0x200d0
#define XCM_REG_PHYS_QNUM3_0					 0x20100
#define XCM_REG_PHYS_QNUM3_1					 0x20104
/* [RW 8] The Event ID for Timers formatting in case of stop done. */
#define XCM_REG_STOP_EVNT_ID					 0x200b8
/* [RC 1] Set at message length mismatch (relative to last indication) at
   the STORM interface. */
#define XCM_REG_STORM_LENGTH_MIS				 0x2021c
/* [RW 3] The weight of the STORM input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_STORM_WEIGHT					 0x200bc
/* [RW 1] STORM - CM Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_STORM_XCM_IFEN					 0x20010
/* [RW 4] Timers output initial credit. Max credit available - 15.Write
   writes the initial credit value; read returns the current value of the
   credit counter. Must be initialized to 4 at start-up. */
#define XCM_REG_TM_INIT_CRD					 0x2041c
/* [RW 3] The weight of the Timers input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_TM_WEIGHT					 0x200ec
/* [RW 28] The CM header for Timers expiration command. */
#define XCM_REG_TM_XCM_HDR					 0x200a8
/* [RW 1] Timers - CM Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_TM_XCM_IFEN					 0x2001c
/* [RW 1] Input tsem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_TSEM_IFEN					 0x20024
/* [RC 1] Set at message length mismatch (relative to last indication) at
   the tsem interface. */
#define XCM_REG_TSEM_LENGTH_MIS 				 0x20224
/* [RW 3] The weight of the input tsem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_TSEM_WEIGHT					 0x200c0
/* [RW 2] The queue index for registration on UNA greater NXT decision rule. */
#define XCM_REG_UNA_GT_NXT_Q					 0x20120
/* [RW 1] Input usem Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_USEM_IFEN					 0x2002c
/* [RC 1] Message length mismatch (relative to last indication) at the usem
   interface. */
#define XCM_REG_USEM_LENGTH_MIS 				 0x2022c
/* [RW 3] The weight of the input usem in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_USEM_WEIGHT					 0x200c8
#define XCM_REG_WU_DA_CNT_CMD00 				 0x201d4
#define XCM_REG_WU_DA_CNT_CMD01 				 0x201d8
#define XCM_REG_WU_DA_CNT_CMD10 				 0x201dc
#define XCM_REG_WU_DA_CNT_CMD11 				 0x201e0
#define XCM_REG_WU_DA_CNT_UPD_VAL00				 0x201e4
#define XCM_REG_WU_DA_CNT_UPD_VAL01				 0x201e8
#define XCM_REG_WU_DA_CNT_UPD_VAL10				 0x201ec
#define XCM_REG_WU_DA_CNT_UPD_VAL11				 0x201f0
#define XCM_REG_WU_DA_SET_TMR_CNT_FLG_CMD00			 0x201c4
#define XCM_REG_WU_DA_SET_TMR_CNT_FLG_CMD01			 0x201c8
#define XCM_REG_WU_DA_SET_TMR_CNT_FLG_CMD10			 0x201cc
#define XCM_REG_WU_DA_SET_TMR_CNT_FLG_CMD11			 0x201d0
/* [RW 1] CM - CFC Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define XCM_REG_XCM_CFC_IFEN					 0x20050
/* [RW 14] Interrupt mask register #0 read/write */
#define XCM_REG_XCM_INT_MASK					 0x202b4
/* [R 14] Interrupt register #0 read */
#define XCM_REG_XCM_INT_STS					 0x202a8
/* [RW 30] Parity mask register #0 read/write */
#define XCM_REG_XCM_PRTY_MASK					 0x202c4
/* [R 30] Parity register #0 read */
#define XCM_REG_XCM_PRTY_STS					 0x202b8
/* [RC 30] Parity register #0 read clear */
#define XCM_REG_XCM_PRTY_STS_CLR				 0x202bc

/* [RW 4] The size of AG context region 0 in REG-pairs. Designates the MS
   REG-pair number (e.g. if region 0 is 6 REG-pairs; the value should be 5).
   Is used to determine the number of the AG context REG-pairs written back;
   when the Reg1WbFlg isn't set. */
#define XCM_REG_XCM_REG0_SZ					 0x200f4
/* [RW 1] CM - STORM 0 Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define XCM_REG_XCM_STORM0_IFEN 				 0x20004
/* [RW 1] CM - STORM 1 Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define XCM_REG_XCM_STORM1_IFEN 				 0x20008
/* [RW 1] CM - Timers Interface enable. If 0 - the valid input is
   disregarded; acknowledge output is deasserted; all other signals are
   treated as usual; if 1 - normal activity. */
#define XCM_REG_XCM_TM_IFEN					 0x20020
/* [RW 1] CM - QM Interface enable. If 0 - the acknowledge input is
   disregarded; valid is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define XCM_REG_XCM_XQM_IFEN					 0x2000c
/* [RW 1] If set the Q index; received from the QM is inserted to event ID. */
#define XCM_REG_XCM_XQM_USE_Q					 0x200f0
/* [RW 4] The value by which CFC updates the activity counter at QM bypass. */
#define XCM_REG_XQM_BYP_ACT_UPD 				 0x200fc
/* [RW 6] QM output initial credit. Max credit available - 32.Write writes
   the initial credit value; read returns the current value of the credit
   counter. Must be initialized to 32 at start-up. */
#define XCM_REG_XQM_INIT_CRD					 0x20420
/* [RW 3] The weight of the QM (primary) input in the WRR mechanism. 0
   stands for weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_XQM_P_WEIGHT					 0x200e4
/* [RW 3] The weight of the QM (secondary) input in the WRR mechanism. 0
   stands for weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_XQM_S_WEIGHT					 0x200e8
/* [RW 28] The CM header value for QM request (primary). */
#define XCM_REG_XQM_XCM_HDR_P					 0x200a0
/* [RW 28] The CM header value for QM request (secondary). */
#define XCM_REG_XQM_XCM_HDR_S					 0x200a4
/* [RW 1] QM - CM Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define XCM_REG_XQM_XCM_IFEN					 0x20014
/* [RW 1] Input SDM Interface enable. If 0 - the valid input is disregarded;
   acknowledge output is deasserted; all other signals are treated as usual;
   if 1 - normal activity. */
#define XCM_REG_XSDM_IFEN					 0x20018
/* [RC 1] Set at message length mismatch (relative to last indication) at
   the SDM interface. */
#define XCM_REG_XSDM_LENGTH_MIS 				 0x20220
/* [RW 3] The weight of the SDM input in the WRR mechanism. 0 stands for
   weight 8 (the most prioritised); 1 stands for weight 1(least
   prioritised); 2 stands for weight 2; tc. */
#define XCM_REG_XSDM_WEIGHT					 0x200e0
/* [RW 17] Indirect access to the descriptor table of the XX protection
   mechanism. The fields are: [5:0] - message length; 11:6] - message
   pointer; 16:12] - next pointer. */
#define XCM_REG_XX_DESCR_TABLE					 0x20480
#define XCM_REG_XX_DESCR_TABLE_SIZE				 32
/* [R 6] Used to read the XX protection Free counter. */
#define XCM_REG_XX_FREE 					 0x20240
/* [RW 6] Initial value for the credit counter; responsible for fulfilling
   of the Input Stage XX protection buffer by the XX protection pending
   messages. Max credit available - 3.Write writes the initial credit value;
   read returns the current value of the credit counter. Must be initialized
   to 2 at start-up. */
#define XCM_REG_XX_INIT_CRD					 0x20424
/* [RW 6] The maximum number of pending messages; which may be stored in XX
   protection. ~xcm_registers_xx_free.xx_free read on read. */
#define XCM_REG_XX_MSG_NUM					 0x20428
/* [RW 8] The Event ID; sent to the STORM in case of XX overflow. */
#define XCM_REG_XX_OVFL_EVNT_ID 				 0x20058
#define XMAC_CLEAR_RX_LSS_STATUS_REG_CLEAR_LOCAL_FAULT_STATUS	 (0x1<<0)
#define XMAC_CLEAR_RX_LSS_STATUS_REG_CLEAR_REMOTE_FAULT_STATUS	 (0x1<<1)
#define XMAC_CTRL_REG_LINE_LOCAL_LPBK				 (0x1<<2)
#define XMAC_CTRL_REG_RX_EN					 (0x1<<1)
#define XMAC_CTRL_REG_SOFT_RESET				 (0x1<<6)
#define XMAC_CTRL_REG_TX_EN					 (0x1<<0)
#define XMAC_PAUSE_CTRL_REG_RX_PAUSE_EN				 (0x1<<18)
#define XMAC_PAUSE_CTRL_REG_TX_PAUSE_EN				 (0x1<<17)
#define XMAC_PFC_CTRL_HI_REG_FORCE_PFC_XON			 (0x1<<1)
#define XMAC_PFC_CTRL_HI_REG_PFC_REFRESH_EN			 (0x1<<0)
#define XMAC_PFC_CTRL_HI_REG_PFC_STATS_EN			 (0x1<<3)
#define XMAC_PFC_CTRL_HI_REG_RX_PFC_EN				 (0x1<<4)
#define XMAC_PFC_CTRL_HI_REG_TX_PFC_EN				 (0x1<<5)
#define XMAC_REG_CLEAR_RX_LSS_STATUS				 0x60
#define XMAC_REG_CTRL						 0
/* [RW 16] Upper 48 bits of ctrl_sa register. Used as the SA in PAUSE/PFC
 * packets transmitted by the MAC */
#define XMAC_REG_CTRL_SA_HI					 0x2c
/* [RW 32] Lower 48 bits of ctrl_sa register. Used as the SA in PAUSE/PFC
 * packets transmitted by the MAC */
#define XMAC_REG_CTRL_SA_LO					 0x28
#define XMAC_REG_PAUSE_CTRL					 0x68
#define XMAC_REG_PFC_CTRL					 0x70
#define XMAC_REG_PFC_CTRL_HI					 0x74
#define XMAC_REG_RX_LSS_STATUS					 0x58
/* [RW 14] Maximum packet size in receive direction; exclusive of preamble &
 * CRC in strip mode */
#define XMAC_REG_RX_MAX_SIZE					 0x40
#define XMAC_REG_TX_CTRL					 0x20
/* [RW 16] Indirect access to the XX table of the XX protection mechanism.
   The fields are:[4:0] - tail pointer; 9:5] - Link List size; 14:10] -
   header pointer. */
#define XCM_REG_XX_TABLE					 0x20500
/* [RW 8] The event id for aggregated interrupt 0 */
#define XSDM_REG_AGG_INT_EVENT_0				 0x166038
#define XSDM_REG_AGG_INT_EVENT_1				 0x16603c
#define XSDM_REG_AGG_INT_EVENT_10				 0x166060
#define XSDM_REG_AGG_INT_EVENT_11				 0x166064
#define XSDM_REG_AGG_INT_EVENT_12				 0x166068
#define XSDM_REG_AGG_INT_EVENT_13				 0x16606c
#define XSDM_REG_AGG_INT_EVENT_14				 0x166070
#define XSDM_REG_AGG_INT_EVENT_2				 0x166040
#define XSDM_REG_AGG_INT_EVENT_3				 0x166044
#define XSDM_REG_AGG_INT_EVENT_4				 0x166048
#define XSDM_REG_AGG_INT_EVENT_5				 0x16604c
#define XSDM_REG_AGG_INT_EVENT_6				 0x166050
#define XSDM_REG_AGG_INT_EVENT_7				 0x166054
#define XSDM_REG_AGG_INT_EVENT_8				 0x166058
#define XSDM_REG_AGG_INT_EVENT_9				 0x16605c
/* [RW 1] For each aggregated interrupt index whether the mode is normal (0)
   or auto-mask-mode (1) */
#define XSDM_REG_AGG_INT_MODE_0 				 0x1661b8
#define XSDM_REG_AGG_INT_MODE_1 				 0x1661bc
/* [RW 13] The start address in the internal RAM for the cfc_rsp lcid */
#define XSDM_REG_CFC_RSP_START_ADDR				 0x166008
/* [RW 16] The maximum value of the completion counter #0 */
#define XSDM_REG_CMP_COUNTER_MAX0				 0x16601c
/* [RW 16] The maximum value of the completion counter #1 */
#define XSDM_REG_CMP_COUNTER_MAX1				 0x166020
/* [RW 16] The maximum value of the completion counter #2 */
#define XSDM_REG_CMP_COUNTER_MAX2				 0x166024
/* [RW 16] The maximum value of the completion counter #3 */
#define XSDM_REG_CMP_COUNTER_MAX3				 0x166028
/* [RW 13] The start address in the internal RAM for the completion
   counters. */
#define XSDM_REG_CMP_COUNTER_START_ADDR 			 0x16600c
#define XSDM_REG_ENABLE_IN1					 0x166238
#define XSDM_REG_ENABLE_IN2					 0x16623c
#define XSDM_REG_ENABLE_OUT1					 0x166240
#define XSDM_REG_ENABLE_OUT2					 0x166244
/* [RW 4] The initial number of messages that can be sent to the pxp control
   interface without receiving any ACK. */
#define XSDM_REG_INIT_CREDIT_PXP_CTRL				 0x1664bc
/* [ST 32] The number of ACK after placement messages received */
#define XSDM_REG_NUM_OF_ACK_AFTER_PLACE 			 0x16627c
/* [ST 32] The number of packet end messages received from the parser */
#define XSDM_REG_NUM_OF_PKT_END_MSG				 0x166274
/* [ST 32] The number of requests received from the pxp async if */
#define XSDM_REG_NUM_OF_PXP_ASYNC_REQ				 0x166278
/* [ST 32] The number of commands received in queue 0 */
#define XSDM_REG_NUM_OF_Q0_CMD					 0x166248
/* [ST 32] The number of commands received in queue 10 */
#define XSDM_REG_NUM_OF_Q10_CMD 				 0x16626c
/* [ST 32] The number of commands received in queue 11 */
#define XSDM_REG_NUM_OF_Q11_CMD 				 0x166270
/* [ST 32] The number of commands received in queue 1 */
#define XSDM_REG_NUM_OF_Q1_CMD					 0x16624c
/* [ST 32] The number of commands received in queue 3 */
#define XSDM_REG_NUM_OF_Q3_CMD					 0x166250
/* [ST 32] The number of commands received in queue 4 */
#define XSDM_REG_NUM_OF_Q4_CMD					 0x166254
/* [ST 32] The number of commands received in queue 5 */
#define XSDM_REG_NUM_OF_Q5_CMD					 0x166258
/* [ST 32] The number of commands received in queue 6 */
#define XSDM_REG_NUM_OF_Q6_CMD					 0x16625c
/* [ST 32] The number of commands received in queue 7 */
#define XSDM_REG_NUM_OF_Q7_CMD					 0x166260
/* [ST 32] The number of commands received in queue 8 */
#define XSDM_REG_NUM_OF_Q8_CMD					 0x166264
/* [ST 32] The number of commands received in queue 9 */
#define XSDM_REG_NUM_OF_Q9_CMD					 0x166268
/* [RW 13] The start address in the internal RAM for queue counters */
#define XSDM_REG_Q_COUNTER_START_ADDR				 0x166010
/* [W 17] Generate an operation after completion; bit-16 is
 * AggVectIdx_valid; bits 15:8 are AggVectIdx; bits 7:5 are the TRIG and
 * bits 4:0 are the T124Param[4:0] */
#define XSDM_REG_OPERATION_GEN					 0x1664c4
/* [R 1] pxp_ctrl rd_data fifo empty in sdm_dma_rsp block */
#define XSDM_REG_RSP_PXP_CTRL_RDATA_EMPTY			 0x166548
/* [R 1] parser fifo empty in sdm_sync block */
#define XSDM_REG_SYNC_PARSER_EMPTY				 0x166550
/* [R 1] parser serial fifo empty in sdm_sync block */
#define XSDM_REG_SYNC_SYNC_EMPTY				 0x166558
/* [RW 32] Tick for timer counter. Applicable only when
   ~xsdm_registers_timer_tick_enable.timer_tick_enable =1 */
#define XSDM_REG_TIMER_TICK					 0x166000
/* [RW 32] Interrupt mask register #0 read/write */
#define XSDM_REG_XSDM_INT_MASK_0				 0x16629c
#define XSDM_REG_XSDM_INT_MASK_1				 0x1662ac
/* [R 32] Interrupt register #0 read */
#define XSDM_REG_XSDM_INT_STS_0 				 0x166290
#define XSDM_REG_XSDM_INT_STS_1 				 0x1662a0
/* [RW 11] Parity mask register #0 read/write */
#define XSDM_REG_XSDM_PRTY_MASK 				 0x1662bc
/* [R 11] Parity register #0 read */
#define XSDM_REG_XSDM_PRTY_STS					 0x1662b0
/* [RC 11] Parity register #0 read clear */
#define XSDM_REG_XSDM_PRTY_STS_CLR				 0x1662b4
/* [RW 5] The number of time_slots in the arbitration cycle */
#define XSEM_REG_ARB_CYCLE_SIZE 				 0x280034
/* [RW 3] The source that is associated with arbitration element 0. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2 */
#define XSEM_REG_ARB_ELEMENT0					 0x280020
/* [RW 3] The source that is associated with arbitration element 1. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~xsem_registers_arb_element0.arb_element0 */
#define XSEM_REG_ARB_ELEMENT1					 0x280024
/* [RW 3] The source that is associated with arbitration element 2. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~xsem_registers_arb_element0.arb_element0
   and ~xsem_registers_arb_element1.arb_element1 */
#define XSEM_REG_ARB_ELEMENT2					 0x280028
/* [RW 3] The source that is associated with arbitration element 3. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.Could
   not be equal to register ~xsem_registers_arb_element0.arb_element0 and
   ~xsem_registers_arb_element1.arb_element1 and
   ~xsem_registers_arb_element2.arb_element2 */
#define XSEM_REG_ARB_ELEMENT3					 0x28002c
/* [RW 3] The source that is associated with arbitration element 4. Source
   decoding is: 0- foc0; 1-fic1; 2-sleeping thread with priority 0; 3-
   sleeping thread with priority 1; 4- sleeping thread with priority 2.
   Could not be equal to register ~xsem_registers_arb_element0.arb_element0
   and ~xsem_registers_arb_element1.arb_element1 and
   ~xsem_registers_arb_element2.arb_element2 and
   ~xsem_registers_arb_element3.arb_element3 */
#define XSEM_REG_ARB_ELEMENT4					 0x280030
#define XSEM_REG_ENABLE_IN					 0x2800a4
#define XSEM_REG_ENABLE_OUT					 0x2800a8
/* [RW 32] This address space contains all registers and memories that are
   placed in SEM_FAST block. The SEM_FAST registers are described in
   appendix B. In order to access the sem_fast registers the base address
   ~fast_memory.fast_memory should be added to eachsem_fast register offset. */
#define XSEM_REG_FAST_MEMORY					 0x2a0000
/* [RW 1] Disables input messages from FIC0 May be updated during run_time
   by the microcode */
#define XSEM_REG_FIC0_DISABLE					 0x280224
/* [RW 1] Disables input messages from FIC1 May be updated during run_time
   by the microcode */
#define XSEM_REG_FIC1_DISABLE					 0x280234
/* [RW 15] Interrupt table Read and write access to it is not possible in
   the middle of the work */
#define XSEM_REG_INT_TABLE					 0x280400
/* [ST 24] Statistics register. The number of messages that entered through
   FIC0 */
#define XSEM_REG_MSG_NUM_FIC0					 0x280000
/* [ST 24] Statistics register. The number of messages that entered through
   FIC1 */
#define XSEM_REG_MSG_NUM_FIC1					 0x280004
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC0 */
#define XSEM_REG_MSG_NUM_FOC0					 0x280008
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC1 */
#define XSEM_REG_MSG_NUM_FOC1					 0x28000c
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC2 */
#define XSEM_REG_MSG_NUM_FOC2					 0x280010
/* [ST 24] Statistics register. The number of messages that were sent to
   FOC3 */
#define XSEM_REG_MSG_NUM_FOC3					 0x280014
/* [RW 1] Disables input messages from the passive buffer May be updated
   during run_time by the microcode */
#define XSEM_REG_PAS_DISABLE					 0x28024c
/* [WB 128] Debug only. Passive buffer memory */
#define XSEM_REG_PASSIVE_BUFFER 				 0x282000
/* [WB 46] pram memory. B45 is parity; b[44:0] - data. */
#define XSEM_REG_PRAM						 0x2c0000
/* [R 16] Valid sleeping threads indication have bit per thread */
#define XSEM_REG_SLEEP_THREADS_VALID				 0x28026c
/* [R 1] EXT_STORE FIFO is empty in sem_slow_ls_ext */
#define XSEM_REG_SLOW_EXT_STORE_EMPTY				 0x2802a0
/* [RW 16] List of free threads . There is a bit per thread. */
#define XSEM_REG_THREADS_LIST					 0x2802e4
/* [RW 3] The arbitration scheme of time_slot 0 */
#define XSEM_REG_TS_0_AS					 0x280038
/* [RW 3] The arbitration scheme of time_slot 10 */
#define XSEM_REG_TS_10_AS					 0x280060
/* [RW 3] The arbitration scheme of time_slot 11 */
#define XSEM_REG_TS_11_AS					 0x280064
/* [RW 3] The arbitration scheme of time_slot 12 */
#define XSEM_REG_TS_12_AS					 0x280068
/* [RW 3] The arbitration scheme of time_slot 13 */
#define XSEM_REG_TS_13_AS					 0x28006c
/* [RW 3] The arbitration scheme of time_slot 14 */
#define XSEM_REG_TS_14_AS					 0x280070
/* [RW 3] The arbitration scheme of time_slot 15 */
#define XSEM_REG_TS_15_AS					 0x280074
/* [RW 3] The arbitration scheme of time_slot 16 */
#define XSEM_REG_TS_16_AS					 0x280078
/* [RW 3] The arbitration scheme of time_slot 17 */
#define XSEM_REG_TS_17_AS					 0x28007c
/* [RW 3] The arbitration scheme of time_slot 18 */
#define XSEM_REG_TS_18_AS					 0x280080
/* [RW 3] The arbitration scheme of time_slot 1 */
#define XSEM_REG_TS_1_AS					 0x28003c
/* [RW 3] The arbitration scheme of time_slot 2 */
#define XSEM_REG_TS_2_AS					 0x280040
/* [RW 3] The arbitration scheme of time_slot 3 */
#define XSEM_REG_TS_3_AS					 0x280044
/* [RW 3] The arbitration scheme of time_slot 4 */
#define XSEM_REG_TS_4_AS					 0x280048
/* [RW 3] The arbitration scheme of time_slot 5 */
#define XSEM_REG_TS_5_AS					 0x28004c
/* [RW 3] The arbitration scheme of time_slot 6 */
#define XSEM_REG_TS_6_AS					 0x280050
/* [RW 3] The arbitration scheme of time_slot 7 */
#define XSEM_REG_TS_7_AS					 0x280054
/* [RW 3] The arbitration scheme of time_slot 8 */
#define XSEM_REG_TS_8_AS					 0x280058
/* [RW 3] The arbitration scheme of time_slot 9 */
#define XSEM_REG_TS_9_AS					 0x28005c
/* [W 7] VF or PF ID for reset error bit. Values 0-63 reset error bit for 64
 * VF; values 64-67 reset error for 4 PF; values 68-127 are not valid. */
#define XSEM_REG_VFPF_ERR_NUM					 0x280380
/* [RW 32] Interrupt mask register #0 read/write */
#define XSEM_REG_XSEM_INT_MASK_0				 0x280110
#define XSEM_REG_XSEM_INT_MASK_1				 0x280120
/* [R 32] Interrupt register #0 read */
#define XSEM_REG_XSEM_INT_STS_0 				 0x280104
#define XSEM_REG_XSEM_INT_STS_1 				 0x280114
/* [RW 32] Parity mask register #0 read/write */
#define XSEM_REG_XSEM_PRTY_MASK_0				 0x280130
#define XSEM_REG_XSEM_PRTY_MASK_1				 0x280140
/* [R 32] Parity register #0 read */
#define XSEM_REG_XSEM_PRTY_STS_0				 0x280124
#define XSEM_REG_XSEM_PRTY_STS_1				 0x280134
/* [RC 32] Parity register #0 read clear */
#define XSEM_REG_XSEM_PRTY_STS_CLR_0				 0x280128
#define XSEM_REG_XSEM_PRTY_STS_CLR_1				 0x280138
#define MCPR_ACCESS_LOCK_LOCK					 (1L<<31)
#define MCPR_NVM_ACCESS_ENABLE_EN				 (1L<<0)
#define MCPR_NVM_ACCESS_ENABLE_WR_EN				 (1L<<1)
#define MCPR_NVM_ADDR_NVM_ADDR_VALUE				 (0xffffffL<<0)
#define MCPR_NVM_CFG4_FLASH_SIZE				 (0x7L<<0)
#define MCPR_NVM_COMMAND_DOIT					 (1L<<4)
#define MCPR_NVM_COMMAND_DONE					 (1L<<3)
#define MCPR_NVM_COMMAND_FIRST					 (1L<<7)
#define MCPR_NVM_COMMAND_LAST					 (1L<<8)
#define MCPR_NVM_COMMAND_WR					 (1L<<5)
#define MCPR_NVM_SW_ARB_ARB_ARB1				 (1L<<9)
#define MCPR_NVM_SW_ARB_ARB_REQ_CLR1				 (1L<<5)
#define MCPR_NVM_SW_ARB_ARB_REQ_SET1				 (1L<<1)
#define BIGMAC_REGISTER_BMAC_CONTROL				 (0x00<<3)
#define BIGMAC_REGISTER_BMAC_XGXS_CONTROL			 (0x01<<3)
#define BIGMAC_REGISTER_CNT_MAX_SIZE				 (0x05<<3)
#define BIGMAC_REGISTER_RX_CONTROL				 (0x21<<3)
#define BIGMAC_REGISTER_RX_LLFC_MSG_FLDS			 (0x46<<3)
#define BIGMAC_REGISTER_RX_LSS_STATUS				 (0x43<<3)
#define BIGMAC_REGISTER_RX_MAX_SIZE				 (0x23<<3)
#define BIGMAC_REGISTER_RX_STAT_GR64				 (0x26<<3)
#define BIGMAC_REGISTER_RX_STAT_GRIPJ				 (0x42<<3)
#define BIGMAC_REGISTER_TX_CONTROL				 (0x07<<3)
#define BIGMAC_REGISTER_TX_MAX_SIZE				 (0x09<<3)
#define BIGMAC_REGISTER_TX_PAUSE_THRESHOLD			 (0x0A<<3)
#define BIGMAC_REGISTER_TX_SOURCE_ADDR				 (0x08<<3)
#define BIGMAC_REGISTER_TX_STAT_GTBYT				 (0x20<<3)
#define BIGMAC_REGISTER_TX_STAT_GTPKT				 (0x0C<<3)
#define BIGMAC2_REGISTER_BMAC_CONTROL				 (0x00<<3)
#define BIGMAC2_REGISTER_BMAC_XGXS_CONTROL			 (0x01<<3)
#define BIGMAC2_REGISTER_CNT_MAX_SIZE				 (0x05<<3)
#define BIGMAC2_REGISTER_PFC_CONTROL				 (0x06<<3)
#define BIGMAC2_REGISTER_RX_CONTROL				 (0x3A<<3)
#define BIGMAC2_REGISTER_RX_LLFC_MSG_FLDS			 (0x62<<3)
#define BIGMAC2_REGISTER_RX_LSS_STAT				 (0x3E<<3)
#define BIGMAC2_REGISTER_RX_MAX_SIZE				 (0x3C<<3)
#define BIGMAC2_REGISTER_RX_STAT_GR64				 (0x40<<3)
#define BIGMAC2_REGISTER_RX_STAT_GRIPJ				 (0x5f<<3)
#define BIGMAC2_REGISTER_RX_STAT_GRPP				 (0x51<<3)
#define BIGMAC2_REGISTER_TX_CONTROL				 (0x1C<<3)
#define BIGMAC2_REGISTER_TX_MAX_SIZE				 (0x1E<<3)
#define BIGMAC2_REGISTER_TX_PAUSE_CONTROL			 (0x20<<3)
#define BIGMAC2_REGISTER_TX_SOURCE_ADDR			 (0x1D<<3)
#define BIGMAC2_REGISTER_TX_STAT_GTBYT				 (0x39<<3)
#define BIGMAC2_REGISTER_TX_STAT_GTPOK				 (0x22<<3)
#define BIGMAC2_REGISTER_TX_STAT_GTPP				 (0x24<<3)
#define EMAC_LED_1000MB_OVERRIDE				 (1L<<1)
#define EMAC_LED_100MB_OVERRIDE 				 (1L<<2)
#define EMAC_LED_10MB_OVERRIDE					 (1L<<3)
#define EMAC_LED_2500MB_OVERRIDE				 (1L<<12)
#define EMAC_LED_OVERRIDE					 (1L<<0)
#define EMAC_LED_TRAFFIC					 (1L<<6)
#define EMAC_MDIO_COMM_COMMAND_ADDRESS				 (0L<<26)
#define EMAC_MDIO_COMM_COMMAND_READ_22				 (2L<<26)
#define EMAC_MDIO_COMM_COMMAND_READ_45				 (3L<<26)
#define EMAC_MDIO_COMM_COMMAND_WRITE_22				 (1L<<26)
#define EMAC_MDIO_COMM_COMMAND_WRITE_45 			 (1L<<26)
#define EMAC_MDIO_COMM_DATA					 (0xffffL<<0)
#define EMAC_MDIO_COMM_START_BUSY				 (1L<<29)
#define EMAC_MDIO_MODE_AUTO_POLL				 (1L<<4)
#define EMAC_MDIO_MODE_CLAUSE_45				 (1L<<31)
#define EMAC_MDIO_MODE_CLOCK_CNT				 (0x3ffL<<16)
#define EMAC_MDIO_MODE_CLOCK_CNT_BITSHIFT			 16
#define EMAC_MDIO_STATUS_10MB					 (1L<<1)
#define EMAC_MODE_25G_MODE					 (1L<<5)
#define EMAC_MODE_HALF_DUPLEX					 (1L<<1)
#define EMAC_MODE_PORT_GMII					 (2L<<2)
#define EMAC_MODE_PORT_MII					 (1L<<2)
#define EMAC_MODE_PORT_MII_10M					 (3L<<2)
#define EMAC_MODE_RESET 					 (1L<<0)
#define EMAC_REG_EMAC_LED					 0xc
#define EMAC_REG_EMAC_MAC_MATCH 				 0x10
#define EMAC_REG_EMAC_MDIO_COMM 				 0xac
#define EMAC_REG_EMAC_MDIO_MODE 				 0xb4
#define EMAC_REG_EMAC_MDIO_STATUS				 0xb0
#define EMAC_REG_EMAC_MODE					 0x0
#define EMAC_REG_EMAC_RX_MODE					 0xc8
#define EMAC_REG_EMAC_RX_MTU_SIZE				 0x9c
#define EMAC_REG_EMAC_RX_STAT_AC				 0x180
#define EMAC_REG_EMAC_RX_STAT_AC_28				 0x1f4
#define EMAC_REG_EMAC_RX_STAT_AC_COUNT				 23
#define EMAC_REG_EMAC_TX_MODE					 0xbc
#define EMAC_REG_EMAC_TX_STAT_AC				 0x280
#define EMAC_REG_EMAC_TX_STAT_AC_COUNT				 22
#define EMAC_REG_RX_PFC_MODE					 0x320
#define EMAC_REG_RX_PFC_MODE_PRIORITIES				 (1L<<2)
#define EMAC_REG_RX_PFC_MODE_RX_EN				 (1L<<1)
#define EMAC_REG_RX_PFC_MODE_TX_EN				 (1L<<0)
#define EMAC_REG_RX_PFC_PARAM					 0x324
#define EMAC_REG_RX_PFC_PARAM_OPCODE_BITSHIFT			 0
#define EMAC_REG_RX_PFC_PARAM_PRIORITY_EN_BITSHIFT		 16
#define EMAC_REG_RX_PFC_STATS_XOFF_RCVD				 0x328
#define EMAC_REG_RX_PFC_STATS_XOFF_RCVD_COUNT			 (0xffff<<0)
#define EMAC_REG_RX_PFC_STATS_XOFF_SENT				 0x330
#define EMAC_REG_RX_PFC_STATS_XOFF_SENT_COUNT			 (0xffff<<0)
#define EMAC_REG_RX_PFC_STATS_XON_RCVD				 0x32c
#define EMAC_REG_RX_PFC_STATS_XON_RCVD_COUNT			 (0xffff<<0)
#define EMAC_REG_RX_PFC_STATS_XON_SENT				 0x334
#define EMAC_REG_RX_PFC_STATS_XON_SENT_COUNT			 (0xffff<<0)
#define EMAC_RX_MODE_FLOW_EN					 (1L<<2)
#define EMAC_RX_MODE_KEEP_MAC_CONTROL				 (1L<<3)
#define EMAC_RX_MODE_KEEP_VLAN_TAG				 (1L<<10)
#define EMAC_RX_MODE_PROMISCUOUS				 (1L<<8)
#define EMAC_RX_MODE_RESET					 (1L<<0)
#define EMAC_RX_MTU_SIZE_JUMBO_ENA				 (1L<<31)
#define EMAC_TX_MODE_EXT_PAUSE_EN				 (1L<<3)
#define EMAC_TX_MODE_FLOW_EN					 (1L<<4)
#define EMAC_TX_MODE_RESET					 (1L<<0)
#define MISC_REGISTERS_GPIO_0					 0
#define MISC_REGISTERS_GPIO_1					 1
#define MISC_REGISTERS_GPIO_2					 2
#define MISC_REGISTERS_GPIO_3					 3
#define MISC_REGISTERS_GPIO_CLR_POS				 16
#define MISC_REGISTERS_GPIO_FLOAT				 (0xffL<<24)
#define MISC_REGISTERS_GPIO_FLOAT_POS				 24
#define MISC_REGISTERS_GPIO_HIGH				 1
#define MISC_REGISTERS_GPIO_INPUT_HI_Z				 2
#define MISC_REGISTERS_GPIO_INT_CLR_POS 			 24
#define MISC_REGISTERS_GPIO_INT_OUTPUT_CLR			 0
#define MISC_REGISTERS_GPIO_INT_OUTPUT_SET			 1
#define MISC_REGISTERS_GPIO_INT_SET_POS 			 16
#define MISC_REGISTERS_GPIO_LOW 				 0
#define MISC_REGISTERS_GPIO_OUTPUT_HIGH 			 1
#define MISC_REGISTERS_GPIO_OUTPUT_LOW				 0
#define MISC_REGISTERS_GPIO_PORT_SHIFT				 4
#define MISC_REGISTERS_GPIO_SET_POS				 8
#define MISC_REGISTERS_RESET_REG_1_CLEAR			 0x588
#define MISC_REGISTERS_RESET_REG_1_RST_BRB1			 (0x1<<0)
#define MISC_REGISTERS_RESET_REG_1_RST_DORQ			 (0x1<<19)
#define MISC_REGISTERS_RESET_REG_1_RST_HC			 (0x1<<29)
#define MISC_REGISTERS_RESET_REG_1_RST_NIG			 (0x1<<7)
#define MISC_REGISTERS_RESET_REG_1_RST_PXP			 (0x1<<26)
#define MISC_REGISTERS_RESET_REG_1_RST_PXPV			 (0x1<<27)
#define MISC_REGISTERS_RESET_REG_1_SET				 0x584
#define MISC_REGISTERS_RESET_REG_2_CLEAR			 0x598
#define MISC_REGISTERS_RESET_REG_2_MSTAT0			 (0x1<<24)
#define MISC_REGISTERS_RESET_REG_2_MSTAT1			 (0x1<<25)
#define MISC_REGISTERS_RESET_REG_2_PGLC				 (0x1<<19)
#define MISC_REGISTERS_RESET_REG_2_RST_ATC			 (0x1<<17)
#define MISC_REGISTERS_RESET_REG_2_RST_BMAC0			 (0x1<<0)
#define MISC_REGISTERS_RESET_REG_2_RST_BMAC1			 (0x1<<1)
#define MISC_REGISTERS_RESET_REG_2_RST_EMAC0			 (0x1<<2)
#define MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE		 (0x1<<14)
#define MISC_REGISTERS_RESET_REG_2_RST_EMAC1			 (0x1<<3)
#define MISC_REGISTERS_RESET_REG_2_RST_EMAC1_HARD_CORE		 (0x1<<15)
#define MISC_REGISTERS_RESET_REG_2_RST_GRC			 (0x1<<4)
#define MISC_REGISTERS_RESET_REG_2_RST_MCP_N_HARD_CORE_RST_B	 (0x1<<6)
#define MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_CMN_CORE	 (0x1<<8)
#define MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_CMN_CPU	 (0x1<<7)
#define MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_REG_HARD_CORE (0x1<<5)
#define MISC_REGISTERS_RESET_REG_2_RST_MDIO			 (0x1<<13)
#define MISC_REGISTERS_RESET_REG_2_RST_MISC_CORE		 (0x1<<11)
#define MISC_REGISTERS_RESET_REG_2_RST_PCI_MDIO			 (0x1<<13)
#define MISC_REGISTERS_RESET_REG_2_RST_RBCN			 (0x1<<9)
#define MISC_REGISTERS_RESET_REG_2_SET				 0x594
#define MISC_REGISTERS_RESET_REG_2_UMAC0			 (0x1<<20)
#define MISC_REGISTERS_RESET_REG_2_UMAC1			 (0x1<<21)
#define MISC_REGISTERS_RESET_REG_2_XMAC				 (0x1<<22)
#define MISC_REGISTERS_RESET_REG_2_XMAC_SOFT			 (0x1<<23)
#define MISC_REGISTERS_RESET_REG_3_CLEAR			 0x5a8
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_SERDES0_IDDQ	 (0x1<<1)
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_SERDES0_PWRDWN	 (0x1<<2)
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_SERDES0_PWRDWN_SD (0x1<<3)
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_SERDES0_RSTB_HW  (0x1<<0)
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_IDDQ	 (0x1<<5)
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_PWRDWN	 (0x1<<6)
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_PWRDWN_SD  (0x1<<7)
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_RSTB_HW	 (0x1<<4)
#define MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_TXD_FIFO_RSTB (0x1<<8)
#define MISC_REGISTERS_RESET_REG_3_SET				 0x5a4
#define MISC_REGISTERS_SPIO_4					 4
#define MISC_REGISTERS_SPIO_5					 5
#define MISC_REGISTERS_SPIO_7					 7
#define MISC_REGISTERS_SPIO_CLR_POS				 16
#define MISC_REGISTERS_SPIO_FLOAT				 (0xffL<<24)
#define MISC_REGISTERS_SPIO_FLOAT_POS				 24
#define MISC_REGISTERS_SPIO_INPUT_HI_Z				 2
#define MISC_REGISTERS_SPIO_INT_OLD_SET_POS			 16
#define MISC_REGISTERS_SPIO_OUTPUT_HIGH 			 1
#define MISC_REGISTERS_SPIO_OUTPUT_LOW				 0
#define MISC_REGISTERS_SPIO_SET_POS				 8
#define HW_LOCK_MAX_RESOURCE_VALUE				 31
#define HW_LOCK_RESOURCE_DRV_FLAGS				 10
#define HW_LOCK_RESOURCE_GPIO					 1
#define HW_LOCK_RESOURCE_MDIO					 0
#define HW_LOCK_RESOURCE_NVRAM					 12
#define HW_LOCK_RESOURCE_PORT0_ATT_MASK				 3
#define HW_LOCK_RESOURCE_RECOVERY_LEADER_0			 8
#define HW_LOCK_RESOURCE_RECOVERY_LEADER_1			 9
#define HW_LOCK_RESOURCE_RECOVERY_REG				 11
#define HW_LOCK_RESOURCE_RESET					 5
#define HW_LOCK_RESOURCE_SPIO					 2
#define AEU_INPUTS_ATTN_BITS_ATC_HW_INTERRUPT			 (0x1<<4)
#define AEU_INPUTS_ATTN_BITS_ATC_PARITY_ERROR			 (0x1<<5)
#define AEU_INPUTS_ATTN_BITS_BRB_PARITY_ERROR			 (0x1<<18)
#define AEU_INPUTS_ATTN_BITS_CCM_HW_INTERRUPT			 (0x1<<31)
#define AEU_INPUTS_ATTN_BITS_CCM_PARITY_ERROR			 (0x1<<30)
#define AEU_INPUTS_ATTN_BITS_CDU_HW_INTERRUPT			 (0x1<<9)
#define AEU_INPUTS_ATTN_BITS_CDU_PARITY_ERROR			 (0x1<<8)
#define AEU_INPUTS_ATTN_BITS_CFC_HW_INTERRUPT			 (0x1<<7)
#define AEU_INPUTS_ATTN_BITS_CFC_PARITY_ERROR			 (0x1<<6)
#define AEU_INPUTS_ATTN_BITS_CSDM_HW_INTERRUPT			 (0x1<<29)
#define AEU_INPUTS_ATTN_BITS_CSDM_PARITY_ERROR			 (0x1<<28)
#define AEU_INPUTS_ATTN_BITS_CSEMI_HW_INTERRUPT			 (0x1<<1)
#define AEU_INPUTS_ATTN_BITS_CSEMI_PARITY_ERROR			 (0x1<<0)
#define AEU_INPUTS_ATTN_BITS_DEBUG_PARITY_ERROR			 (0x1<<18)
#define AEU_INPUTS_ATTN_BITS_DMAE_HW_INTERRUPT			 (0x1<<11)
#define AEU_INPUTS_ATTN_BITS_DMAE_PARITY_ERROR			 (0x1<<10)
#define AEU_INPUTS_ATTN_BITS_DOORBELLQ_HW_INTERRUPT		 (0x1<<13)
#define AEU_INPUTS_ATTN_BITS_DOORBELLQ_PARITY_ERROR		 (0x1<<12)
#define AEU_INPUTS_ATTN_BITS_GPIO0_FUNCTION_0			 (0x1<<2)
#define AEU_INPUTS_ATTN_BITS_IGU_PARITY_ERROR			 (0x1<<12)
#define AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY		 (0x1<<28)
#define AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY		 (0x1<<31)
#define AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY		 (0x1<<29)
#define AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY		 (0x1<<30)
#define AEU_INPUTS_ATTN_BITS_MISC_HW_INTERRUPT			 (0x1<<15)
#define AEU_INPUTS_ATTN_BITS_MISC_PARITY_ERROR			 (0x1<<14)
#define AEU_INPUTS_ATTN_BITS_NIG_PARITY_ERROR			 (0x1<<14)
#define AEU_INPUTS_ATTN_BITS_PARSER_PARITY_ERROR		 (0x1<<20)
#define AEU_INPUTS_ATTN_BITS_PBCLIENT_HW_INTERRUPT		 (0x1<<31)
#define AEU_INPUTS_ATTN_BITS_PBCLIENT_PARITY_ERROR		 (0x1<<30)
#define AEU_INPUTS_ATTN_BITS_PBF_PARITY_ERROR			 (0x1<<0)
#define AEU_INPUTS_ATTN_BITS_PGLUE_HW_INTERRUPT			 (0x1<<2)
#define AEU_INPUTS_ATTN_BITS_PGLUE_PARITY_ERROR			 (0x1<<3)
#define AEU_INPUTS_ATTN_BITS_PXPPCICLOCKCLIENT_HW_INTERRUPT	 (0x1<<5)
#define AEU_INPUTS_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR	 (0x1<<4)
#define AEU_INPUTS_ATTN_BITS_PXP_HW_INTERRUPT			 (0x1<<3)
#define AEU_INPUTS_ATTN_BITS_PXP_PARITY_ERROR			 (0x1<<2)
#define AEU_INPUTS_ATTN_BITS_QM_HW_INTERRUPT			 (0x1<<3)
#define AEU_INPUTS_ATTN_BITS_QM_PARITY_ERROR			 (0x1<<2)
#define AEU_INPUTS_ATTN_BITS_SEARCHER_PARITY_ERROR		 (0x1<<22)
#define AEU_INPUTS_ATTN_BITS_SPIO5				 (0x1<<15)
#define AEU_INPUTS_ATTN_BITS_TCM_HW_INTERRUPT			 (0x1<<27)
#define AEU_INPUTS_ATTN_BITS_TCM_PARITY_ERROR			 (0x1<<26)
#define AEU_INPUTS_ATTN_BITS_TIMERS_HW_INTERRUPT		 (0x1<<5)
#define AEU_INPUTS_ATTN_BITS_TIMERS_PARITY_ERROR		 (0x1<<4)
#define AEU_INPUTS_ATTN_BITS_TSDM_HW_INTERRUPT			 (0x1<<25)
#define AEU_INPUTS_ATTN_BITS_TSDM_PARITY_ERROR			 (0x1<<24)
#define AEU_INPUTS_ATTN_BITS_TSEMI_HW_INTERRUPT			 (0x1<<29)
#define AEU_INPUTS_ATTN_BITS_TSEMI_PARITY_ERROR			 (0x1<<28)
#define AEU_INPUTS_ATTN_BITS_UCM_HW_INTERRUPT			 (0x1<<23)
#define AEU_INPUTS_ATTN_BITS_UCM_PARITY_ERROR			 (0x1<<22)
#define AEU_INPUTS_ATTN_BITS_UPB_HW_INTERRUPT			 (0x1<<27)
#define AEU_INPUTS_ATTN_BITS_UPB_PARITY_ERROR			 (0x1<<26)
#define AEU_INPUTS_ATTN_BITS_USDM_HW_INTERRUPT			 (0x1<<21)
#define AEU_INPUTS_ATTN_BITS_USDM_PARITY_ERROR			 (0x1<<20)
#define AEU_INPUTS_ATTN_BITS_USEMI_HW_INTERRUPT			 (0x1<<25)
#define AEU_INPUTS_ATTN_BITS_USEMI_PARITY_ERROR			 (0x1<<24)
#define AEU_INPUTS_ATTN_BITS_VAUX_PCI_CORE_PARITY_ERROR		 (0x1<<16)
#define AEU_INPUTS_ATTN_BITS_XCM_HW_INTERRUPT			 (0x1<<9)
#define AEU_INPUTS_ATTN_BITS_XCM_PARITY_ERROR			 (0x1<<8)
#define AEU_INPUTS_ATTN_BITS_XSDM_HW_INTERRUPT			 (0x1<<7)
#define AEU_INPUTS_ATTN_BITS_XSDM_PARITY_ERROR			 (0x1<<6)
#define AEU_INPUTS_ATTN_BITS_XSEMI_HW_INTERRUPT			 (0x1<<11)
#define AEU_INPUTS_ATTN_BITS_XSEMI_PARITY_ERROR			 (0x1<<10)

#define AEU_INPUTS_ATTN_BITS_GPIO3_FUNCTION_0			(0x1<<5)
#define AEU_INPUTS_ATTN_BITS_GPIO3_FUNCTION_1			(0x1<<9)

#define RESERVED_GENERAL_ATTENTION_BIT_0	0

#define EVEREST_GEN_ATTN_IN_USE_MASK		0x7ffe0
#define EVEREST_LATCHED_ATTN_IN_USE_MASK	0xffe00000

#define RESERVED_GENERAL_ATTENTION_BIT_6	6
#define RESERVED_GENERAL_ATTENTION_BIT_7	7
#define RESERVED_GENERAL_ATTENTION_BIT_8	8
#define RESERVED_GENERAL_ATTENTION_BIT_9	9
#define RESERVED_GENERAL_ATTENTION_BIT_10	10
#define RESERVED_GENERAL_ATTENTION_BIT_11	11
#define RESERVED_GENERAL_ATTENTION_BIT_12	12
#define RESERVED_GENERAL_ATTENTION_BIT_13	13
#define RESERVED_GENERAL_ATTENTION_BIT_14	14
#define RESERVED_GENERAL_ATTENTION_BIT_15	15
#define RESERVED_GENERAL_ATTENTION_BIT_16	16
#define RESERVED_GENERAL_ATTENTION_BIT_17	17
#define RESERVED_GENERAL_ATTENTION_BIT_18	18
#define RESERVED_GENERAL_ATTENTION_BIT_19	19
#define RESERVED_GENERAL_ATTENTION_BIT_20	20
#define RESERVED_GENERAL_ATTENTION_BIT_21	21

/* storm asserts attention bits */
#define TSTORM_FATAL_ASSERT_ATTENTION_BIT     RESERVED_GENERAL_ATTENTION_BIT_7
#define USTORM_FATAL_ASSERT_ATTENTION_BIT     RESERVED_GENERAL_ATTENTION_BIT_8
#define CSTORM_FATAL_ASSERT_ATTENTION_BIT     RESERVED_GENERAL_ATTENTION_BIT_9
#define XSTORM_FATAL_ASSERT_ATTENTION_BIT     RESERVED_GENERAL_ATTENTION_BIT_10

/* mcp error attention bit */
#define MCP_FATAL_ASSERT_ATTENTION_BIT	      RESERVED_GENERAL_ATTENTION_BIT_11

/*E1H NIG status sync attention mapped to group 4-7*/
#define LINK_SYNC_ATTENTION_BIT_FUNC_0	    RESERVED_GENERAL_ATTENTION_BIT_12
#define LINK_SYNC_ATTENTION_BIT_FUNC_1	    RESERVED_GENERAL_ATTENTION_BIT_13
#define LINK_SYNC_ATTENTION_BIT_FUNC_2	    RESERVED_GENERAL_ATTENTION_BIT_14
#define LINK_SYNC_ATTENTION_BIT_FUNC_3	    RESERVED_GENERAL_ATTENTION_BIT_15
#define LINK_SYNC_ATTENTION_BIT_FUNC_4	    RESERVED_GENERAL_ATTENTION_BIT_16
#define LINK_SYNC_ATTENTION_BIT_FUNC_5	    RESERVED_GENERAL_ATTENTION_BIT_17
#define LINK_SYNC_ATTENTION_BIT_FUNC_6	    RESERVED_GENERAL_ATTENTION_BIT_18
#define LINK_SYNC_ATTENTION_BIT_FUNC_7	    RESERVED_GENERAL_ATTENTION_BIT_19


#define LATCHED_ATTN_RBCR			23
#define LATCHED_ATTN_RBCT			24
#define LATCHED_ATTN_RBCN			25
#define LATCHED_ATTN_RBCU			26
#define LATCHED_ATTN_RBCP			27
#define LATCHED_ATTN_TIMEOUT_GRC		28
#define LATCHED_ATTN_RSVD_GRC			29
#define LATCHED_ATTN_ROM_PARITY_MCP		30
#define LATCHED_ATTN_UM_RX_PARITY_MCP		31
#define LATCHED_ATTN_UM_TX_PARITY_MCP		32
#define LATCHED_ATTN_SCPAD_PARITY_MCP		33

#define GENERAL_ATTEN_WORD(atten_name)	       ((94 + atten_name) / 32)
#define GENERAL_ATTEN_OFFSET(atten_name)\
	(1UL << ((94 + atten_name) % 32))
/*
 * This file defines GRC base address for every block.
 * This file is included by chipsim, asm microcode and cpp microcode.
 * These values are used in Design.xml on regBase attribute
 * Use the base with the generated offsets of specific registers.
 */

#define GRCBASE_PXPCS		0x000000
#define GRCBASE_PCICONFIG	0x002000
#define GRCBASE_PCIREG		0x002400
#define GRCBASE_EMAC0		0x008000
#define GRCBASE_EMAC1		0x008400
#define GRCBASE_DBU		0x008800
#define GRCBASE_MISC		0x00A000
#define GRCBASE_DBG		0x00C000
#define GRCBASE_NIG		0x010000
#define GRCBASE_XCM		0x020000
#define GRCBASE_PRS		0x040000
#define GRCBASE_SRCH		0x040400
#define GRCBASE_TSDM		0x042000
#define GRCBASE_TCM		0x050000
#define GRCBASE_BRB1		0x060000
#define GRCBASE_MCP		0x080000
#define GRCBASE_UPB		0x0C1000
#define GRCBASE_CSDM		0x0C2000
#define GRCBASE_USDM		0x0C4000
#define GRCBASE_CCM		0x0D0000
#define GRCBASE_UCM		0x0E0000
#define GRCBASE_CDU		0x101000
#define GRCBASE_DMAE		0x102000
#define GRCBASE_PXP		0x103000
#define GRCBASE_CFC		0x104000
#define GRCBASE_HC		0x108000
#define GRCBASE_PXP2		0x120000
#define GRCBASE_PBF		0x140000
#define GRCBASE_UMAC0		0x160000
#define GRCBASE_UMAC1		0x160400
#define GRCBASE_XPB		0x161000
#define GRCBASE_MSTAT0	    0x162000
#define GRCBASE_MSTAT1	    0x162800
#define GRCBASE_XMAC0		0x163000
#define GRCBASE_XMAC1		0x163800
#define GRCBASE_TIMERS		0x164000
#define GRCBASE_XSDM		0x166000
#define GRCBASE_QM		0x168000
#define GRCBASE_DQ		0x170000
#define GRCBASE_TSEM		0x180000
#define GRCBASE_CSEM		0x200000
#define GRCBASE_XSEM		0x280000
#define GRCBASE_USEM		0x300000
#define GRCBASE_MISC_AEU	GRCBASE_MISC


/* offset of configuration space in the pci core register */
#define PCICFG_OFFSET					0x2000
#define PCICFG_VENDOR_ID_OFFSET 			0x00
#define PCICFG_DEVICE_ID_OFFSET 			0x02
#define PCICFG_COMMAND_OFFSET				0x04
#define PCICFG_COMMAND_IO_SPACE 		(1<<0)
#define PCICFG_COMMAND_MEM_SPACE		(1<<1)
#define PCICFG_COMMAND_BUS_MASTER		(1<<2)
#define PCICFG_COMMAND_SPECIAL_CYCLES		(1<<3)
#define PCICFG_COMMAND_MWI_CYCLES		(1<<4)
#define PCICFG_COMMAND_VGA_SNOOP		(1<<5)
#define PCICFG_COMMAND_PERR_ENA 		(1<<6)
#define PCICFG_COMMAND_STEPPING 		(1<<7)
#define PCICFG_COMMAND_SERR_ENA 		(1<<8)
#define PCICFG_COMMAND_FAST_B2B 		(1<<9)
#define PCICFG_COMMAND_INT_DISABLE		(1<<10)
#define PCICFG_COMMAND_RESERVED 		(0x1f<<11)
#define PCICFG_STATUS_OFFSET				0x06
#define PCICFG_REVESION_ID_OFFSET			0x08
#define PCICFG_CACHE_LINE_SIZE				0x0c
#define PCICFG_LATENCY_TIMER				0x0d
#define PCICFG_BAR_1_LOW				0x10
#define PCICFG_BAR_1_HIGH				0x14
#define PCICFG_BAR_2_LOW				0x18
#define PCICFG_BAR_2_HIGH				0x1c
#define PCICFG_SUBSYSTEM_VENDOR_ID_OFFSET		0x2c
#define PCICFG_SUBSYSTEM_ID_OFFSET			0x2e
#define PCICFG_INT_LINE 				0x3c
#define PCICFG_INT_PIN					0x3d
#define PCICFG_PM_CAPABILITY				0x48
#define PCICFG_PM_CAPABILITY_VERSION		(0x3<<16)
#define PCICFG_PM_CAPABILITY_CLOCK		(1<<19)
#define PCICFG_PM_CAPABILITY_RESERVED		(1<<20)
#define PCICFG_PM_CAPABILITY_DSI		(1<<21)
#define PCICFG_PM_CAPABILITY_AUX_CURRENT	(0x7<<22)
#define PCICFG_PM_CAPABILITY_D1_SUPPORT 	(1<<25)
#define PCICFG_PM_CAPABILITY_D2_SUPPORT 	(1<<26)
#define PCICFG_PM_CAPABILITY_PME_IN_D0		(1<<27)
#define PCICFG_PM_CAPABILITY_PME_IN_D1		(1<<28)
#define PCICFG_PM_CAPABILITY_PME_IN_D2		(1<<29)
#define PCICFG_PM_CAPABILITY_PME_IN_D3_HOT	(1<<30)
#define PCICFG_PM_CAPABILITY_PME_IN_D3_COLD	(1<<31)
#define PCICFG_PM_CSR_OFFSET				0x4c
#define PCICFG_PM_CSR_STATE			(0x3<<0)
#define PCICFG_PM_CSR_PME_ENABLE		(1<<8)
#define PCICFG_PM_CSR_PME_STATUS		(1<<15)
#define PCICFG_MSI_CAP_ID_OFFSET			0x58
#define PCICFG_MSI_CONTROL_ENABLE		(0x1<<16)
#define PCICFG_MSI_CONTROL_MCAP 		(0x7<<17)
#define PCICFG_MSI_CONTROL_MENA 		(0x7<<20)
#define PCICFG_MSI_CONTROL_64_BIT_ADDR_CAP	(0x1<<23)
#define PCICFG_MSI_CONTROL_MSI_PVMASK_CAPABLE	(0x1<<24)
#define PCICFG_GRC_ADDRESS				0x78
#define PCICFG_GRC_DATA				0x80
#define PCICFG_ME_REGISTER				0x98
#define PCICFG_MSIX_CAP_ID_OFFSET			0xa0
#define PCICFG_MSIX_CONTROL_TABLE_SIZE		(0x7ff<<16)
#define PCICFG_MSIX_CONTROL_RESERVED		(0x7<<27)
#define PCICFG_MSIX_CONTROL_FUNC_MASK		(0x1<<30)
#define PCICFG_MSIX_CONTROL_MSIX_ENABLE 	(0x1<<31)

#define PCICFG_DEVICE_CONTROL				0xb4
#define PCICFG_DEVICE_STATUS				0xb6
#define PCICFG_DEVICE_STATUS_CORR_ERR_DET	(1<<0)
#define PCICFG_DEVICE_STATUS_NON_FATAL_ERR_DET	(1<<1)
#define PCICFG_DEVICE_STATUS_FATAL_ERR_DET	(1<<2)
#define PCICFG_DEVICE_STATUS_UNSUP_REQ_DET	(1<<3)
#define PCICFG_DEVICE_STATUS_AUX_PWR_DET	(1<<4)
#define PCICFG_DEVICE_STATUS_NO_PEND		(1<<5)
#define PCICFG_LINK_CONTROL				0xbc


#define BAR_USTRORM_INTMEM				0x400000
#define BAR_CSTRORM_INTMEM				0x410000
#define BAR_XSTRORM_INTMEM				0x420000
#define BAR_TSTRORM_INTMEM				0x430000

/* for accessing the IGU in case of status block ACK */
#define BAR_IGU_INTMEM					0x440000

#define BAR_DOORBELL_OFFSET				0x800000

#define BAR_ME_REGISTER 				0x450000

/* config_2 offset */
#define GRC_CONFIG_2_SIZE_REG				0x408
#define PCI_CONFIG_2_BAR1_SIZE			(0xfL<<0)
#define PCI_CONFIG_2_BAR1_SIZE_DISABLED 	(0L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_64K		(1L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_128K		(2L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_256K		(3L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_512K		(4L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_1M		(5L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_2M		(6L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_4M		(7L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_8M		(8L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_16M		(9L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_32M		(10L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_64M		(11L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_128M		(12L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_256M		(13L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_512M		(14L<<0)
#define PCI_CONFIG_2_BAR1_SIZE_1G		(15L<<0)
#define PCI_CONFIG_2_BAR1_64ENA 		(1L<<4)
#define PCI_CONFIG_2_EXP_ROM_RETRY		(1L<<5)
#define PCI_CONFIG_2_CFG_CYCLE_RETRY		(1L<<6)
#define PCI_CONFIG_2_FIRST_CFG_DONE		(1L<<7)
#define PCI_CONFIG_2_EXP_ROM_SIZE		(0xffL<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_DISABLED	(0L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_2K		(1L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_4K		(2L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_8K		(3L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_16K		(4L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_32K		(5L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_64K		(6L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_128K		(7L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_256K		(8L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_512K		(9L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_1M		(10L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_2M		(11L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_4M		(12L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_8M		(13L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_16M		(14L<<8)
#define PCI_CONFIG_2_EXP_ROM_SIZE_32M		(15L<<8)
#define PCI_CONFIG_2_BAR_PREFETCH		(1L<<16)
#define PCI_CONFIG_2_RESERVED0			(0x7fffL<<17)

/* config_3 offset */
#define GRC_CONFIG_3_SIZE_REG				0x40c
#define PCI_CONFIG_3_STICKY_BYTE		(0xffL<<0)
#define PCI_CONFIG_3_FORCE_PME			(1L<<24)
#define PCI_CONFIG_3_PME_STATUS 		(1L<<25)
#define PCI_CONFIG_3_PME_ENABLE 		(1L<<26)
#define PCI_CONFIG_3_PM_STATE			(0x3L<<27)
#define PCI_CONFIG_3_VAUX_PRESET		(1L<<30)
#define PCI_CONFIG_3_PCI_POWER			(1L<<31)

#define GRC_BAR2_CONFIG 				0x4e0
#define PCI_CONFIG_2_BAR2_SIZE			(0xfL<<0)
#define PCI_CONFIG_2_BAR2_SIZE_DISABLED 	(0L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_64K		(1L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_128K		(2L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_256K		(3L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_512K		(4L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_1M		(5L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_2M		(6L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_4M		(7L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_8M		(8L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_16M		(9L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_32M		(10L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_64M		(11L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_128M		(12L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_256M		(13L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_512M		(14L<<0)
#define PCI_CONFIG_2_BAR2_SIZE_1G		(15L<<0)
#define PCI_CONFIG_2_BAR2_64ENA 		(1L<<4)

#define PCI_PM_DATA_A					0x410
#define PCI_PM_DATA_B					0x414
#define PCI_ID_VAL1					0x434
#define PCI_ID_VAL2					0x438

#define PXPCS_TL_CONTROL_5		    0x814
#define PXPCS_TL_CONTROL_5_UNKNOWNTYPE_ERR_ATTN    (1 << 29) /*WC*/
#define PXPCS_TL_CONTROL_5_BOUNDARY4K_ERR_ATTN	   (1 << 28)   /*WC*/
#define PXPCS_TL_CONTROL_5_MRRS_ERR_ATTN   (1 << 27)   /*WC*/
#define PXPCS_TL_CONTROL_5_MPS_ERR_ATTN    (1 << 26)   /*WC*/
#define PXPCS_TL_CONTROL_5_TTX_BRIDGE_FORWARD_ERR  (1 << 25)   /*WC*/
#define PXPCS_TL_CONTROL_5_TTX_TXINTF_OVERFLOW	   (1 << 24)   /*WC*/
#define PXPCS_TL_CONTROL_5_PHY_ERR_ATTN    (1 << 23)   /*RO*/
#define PXPCS_TL_CONTROL_5_DL_ERR_ATTN	   (1 << 22)   /*RO*/
#define PXPCS_TL_CONTROL_5_TTX_ERR_NP_TAG_IN_USE   (1 << 21)   /*WC*/
#define PXPCS_TL_CONTROL_5_TRX_ERR_UNEXP_RTAG  (1 << 20)   /*WC*/
#define PXPCS_TL_CONTROL_5_PRI_SIG_TARGET_ABORT1   (1 << 19)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_UNSPPORT1   (1 << 18)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_ECRC1   (1 << 17)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_MALF_TLP1   (1 << 16)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_RX_OFLOW1   (1 << 15)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_UNEXP_CPL1  (1 << 14)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_MASTER_ABRT1    (1 << 13)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_CPL_TIMEOUT1    (1 << 12)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_FC_PRTL1    (1 << 11)   /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_PSND_TLP1   (1 << 10)   /*WC*/
#define PXPCS_TL_CONTROL_5_PRI_SIG_TARGET_ABORT    (1 << 9)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_UNSPPORT    (1 << 8)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_ECRC    (1 << 7)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_MALF_TLP    (1 << 6)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_RX_OFLOW    (1 << 5)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_UNEXP_CPL   (1 << 4)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_MASTER_ABRT     (1 << 3)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_CPL_TIMEOUT     (1 << 2)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_FC_PRTL	   (1 << 1)    /*WC*/
#define PXPCS_TL_CONTROL_5_ERR_PSND_TLP    (1 << 0)    /*WC*/


#define PXPCS_TL_FUNC345_STAT	   0x854
#define PXPCS_TL_FUNC345_STAT_PRI_SIG_TARGET_ABORT4    (1 << 29)   /* WC */
#define PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT4\
	(1 << 28) /* Unsupported Request Error Status in function4, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_ECRC4\
	(1 << 27) /* ECRC Error TLP Status Status in function 4, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_MALF_TLP4\
	(1 << 26) /* Malformed TLP Status Status in function 4, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_RX_OFLOW4\
	(1 << 25) /* Receiver Overflow Status Status in function 4, if \
	set, generate pcie_err_attn output when this error is seen.. WC \
	*/
#define PXPCS_TL_FUNC345_STAT_ERR_UNEXP_CPL4\
	(1 << 24) /* Unexpected Completion Status Status in function 4, \
	if set, generate pcie_err_attn output when this error is seen. WC \
	*/
#define PXPCS_TL_FUNC345_STAT_ERR_MASTER_ABRT4\
	(1 << 23) /* Receive UR Statusin function 4. If set, generate \
	pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_CPL_TIMEOUT4\
	(1 << 22) /* Completer Timeout Status Status in function 4, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_FC_PRTL4\
	(1 << 21) /* Flow Control Protocol Error Status Status in \
	function 4, if set, generate pcie_err_attn output when this error \
	is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_PSND_TLP4\
	(1 << 20) /* Poisoned Error Status Status in function 4, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC345_STAT_PRI_SIG_TARGET_ABORT3    (1 << 19)   /* WC */
#define PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT3\
	(1 << 18) /* Unsupported Request Error Status in function3, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_ECRC3\
	(1 << 17) /* ECRC Error TLP Status Status in function 3, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_MALF_TLP3\
	(1 << 16) /* Malformed TLP Status Status in function 3, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_RX_OFLOW3\
	(1 << 15) /* Receiver Overflow Status Status in function 3, if \
	set, generate pcie_err_attn output when this error is seen.. WC \
	*/
#define PXPCS_TL_FUNC345_STAT_ERR_UNEXP_CPL3\
	(1 << 14) /* Unexpected Completion Status Status in function 3, \
	if set, generate pcie_err_attn output when this error is seen. WC \
	*/
#define PXPCS_TL_FUNC345_STAT_ERR_MASTER_ABRT3\
	(1 << 13) /* Receive UR Statusin function 3. If set, generate \
	pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_CPL_TIMEOUT3\
	(1 << 12) /* Completer Timeout Status Status in function 3, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_FC_PRTL3\
	(1 << 11) /* Flow Control Protocol Error Status Status in \
	function 3, if set, generate pcie_err_attn output when this error \
	is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_PSND_TLP3\
	(1 << 10) /* Poisoned Error Status Status in function 3, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC345_STAT_PRI_SIG_TARGET_ABORT2    (1 << 9)    /* WC */
#define PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT2\
	(1 << 8) /* Unsupported Request Error Status for Function 2, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_ECRC2\
	(1 << 7) /* ECRC Error TLP Status Status for Function 2, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_MALF_TLP2\
	(1 << 6) /* Malformed TLP Status Status for Function 2, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_RX_OFLOW2\
	(1 << 5) /* Receiver Overflow Status Status for Function 2, if \
	set, generate pcie_err_attn output when this error is seen.. WC \
	*/
#define PXPCS_TL_FUNC345_STAT_ERR_UNEXP_CPL2\
	(1 << 4) /* Unexpected Completion Status Status for Function 2, \
	if set, generate pcie_err_attn output when this error is seen. WC \
	*/
#define PXPCS_TL_FUNC345_STAT_ERR_MASTER_ABRT2\
	(1 << 3) /* Receive UR Statusfor Function 2. If set, generate \
	pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_CPL_TIMEOUT2\
	(1 << 2) /* Completer Timeout Status Status for Function 2, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_FC_PRTL2\
	(1 << 1) /* Flow Control Protocol Error Status Status for \
	Function 2, if set, generate pcie_err_attn output when this error \
	is seen. WC */
#define PXPCS_TL_FUNC345_STAT_ERR_PSND_TLP2\
	(1 << 0) /* Poisoned Error Status Status for Function 2, if set, \
	generate pcie_err_attn output when this error is seen.. WC */


#define PXPCS_TL_FUNC678_STAT  0x85C
#define PXPCS_TL_FUNC678_STAT_PRI_SIG_TARGET_ABORT7    (1 << 29)   /*	 WC */
#define PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT7\
	(1 << 28) /* Unsupported Request Error Status in function7, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_ECRC7\
	(1 << 27) /* ECRC Error TLP Status Status in function 7, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_MALF_TLP7\
	(1 << 26) /* Malformed TLP Status Status in function 7, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_RX_OFLOW7\
	(1 << 25) /* Receiver Overflow Status Status in function 7, if \
	set, generate pcie_err_attn output when this error is seen.. WC \
	*/
#define PXPCS_TL_FUNC678_STAT_ERR_UNEXP_CPL7\
	(1 << 24) /* Unexpected Completion Status Status in function 7, \
	if set, generate pcie_err_attn output when this error is seen. WC \
	*/
#define PXPCS_TL_FUNC678_STAT_ERR_MASTER_ABRT7\
	(1 << 23) /* Receive UR Statusin function 7. If set, generate \
	pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_CPL_TIMEOUT7\
	(1 << 22) /* Completer Timeout Status Status in function 7, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_FC_PRTL7\
	(1 << 21) /* Flow Control Protocol Error Status Status in \
	function 7, if set, generate pcie_err_attn output when this error \
	is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_PSND_TLP7\
	(1 << 20) /* Poisoned Error Status Status in function 7, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC678_STAT_PRI_SIG_TARGET_ABORT6    (1 << 19)    /*	  WC */
#define PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT6\
	(1 << 18) /* Unsupported Request Error Status in function6, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_ECRC6\
	(1 << 17) /* ECRC Error TLP Status Status in function 6, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_MALF_TLP6\
	(1 << 16) /* Malformed TLP Status Status in function 6, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_RX_OFLOW6\
	(1 << 15) /* Receiver Overflow Status Status in function 6, if \
	set, generate pcie_err_attn output when this error is seen.. WC \
	*/
#define PXPCS_TL_FUNC678_STAT_ERR_UNEXP_CPL6\
	(1 << 14) /* Unexpected Completion Status Status in function 6, \
	if set, generate pcie_err_attn output when this error is seen. WC \
	*/
#define PXPCS_TL_FUNC678_STAT_ERR_MASTER_ABRT6\
	(1 << 13) /* Receive UR Statusin function 6. If set, generate \
	pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_CPL_TIMEOUT6\
	(1 << 12) /* Completer Timeout Status Status in function 6, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_FC_PRTL6\
	(1 << 11) /* Flow Control Protocol Error Status Status in \
	function 6, if set, generate pcie_err_attn output when this error \
	is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_PSND_TLP6\
	(1 << 10) /* Poisoned Error Status Status in function 6, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC678_STAT_PRI_SIG_TARGET_ABORT5    (1 << 9) /*    WC */
#define PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT5\
	(1 << 8) /* Unsupported Request Error Status for Function 5, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_ECRC5\
	(1 << 7) /* ECRC Error TLP Status Status for Function 5, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_MALF_TLP5\
	(1 << 6) /* Malformed TLP Status Status for Function 5, if set, \
	generate pcie_err_attn output when this error is seen.. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_RX_OFLOW5\
	(1 << 5) /* Receiver Overflow Status Status for Function 5, if \
	set, generate pcie_err_attn output when this error is seen.. WC \
	*/
#define PXPCS_TL_FUNC678_STAT_ERR_UNEXP_CPL5\
	(1 << 4) /* Unexpected Completion Status Status for Function 5, \
	if set, generate pcie_err_attn output when this error is seen. WC \
	*/
#define PXPCS_TL_FUNC678_STAT_ERR_MASTER_ABRT5\
	(1 << 3) /* Receive UR Statusfor Function 5. If set, generate \
	pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_CPL_TIMEOUT5\
	(1 << 2) /* Completer Timeout Status Status for Function 5, if \
	set, generate pcie_err_attn output when this error is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_FC_PRTL5\
	(1 << 1) /* Flow Control Protocol Error Status Status for \
	Function 5, if set, generate pcie_err_attn output when this error \
	is seen. WC */
#define PXPCS_TL_FUNC678_STAT_ERR_PSND_TLP5\
	(1 << 0) /* Poisoned Error Status Status for Function 5, if set, \
	generate pcie_err_attn output when this error is seen.. WC */


#define BAR_USTRORM_INTMEM				0x400000
#define BAR_CSTRORM_INTMEM				0x410000
#define BAR_XSTRORM_INTMEM				0x420000
#define BAR_TSTRORM_INTMEM				0x430000

/* for accessing the IGU in case of status block ACK */
#define BAR_IGU_INTMEM					0x440000

#define BAR_DOORBELL_OFFSET				0x800000

#define BAR_ME_REGISTER				0x450000
#define ME_REG_PF_NUM_SHIFT		0
#define ME_REG_PF_NUM\
	(7L<<ME_REG_PF_NUM_SHIFT) /* Relative PF Num */
#define ME_REG_VF_VALID		(1<<8)
#define ME_REG_VF_NUM_SHIFT		9
#define ME_REG_VF_NUM_MASK		(0x3f<<ME_REG_VF_NUM_SHIFT)
#define ME_REG_VF_ERR			(0x1<<3)
#define ME_REG_ABS_PF_NUM_SHIFT	16
#define ME_REG_ABS_PF_NUM\
	(7L<<ME_REG_ABS_PF_NUM_SHIFT) /* Absolute PF Num */


#define MDIO_REG_BANK_CL73_IEEEB0	0x0
#define MDIO_CL73_IEEEB0_CL73_AN_CONTROL	0x0
#define MDIO_CL73_IEEEB0_CL73_AN_CONTROL_RESTART_AN	0x0200
#define MDIO_CL73_IEEEB0_CL73_AN_CONTROL_AN_EN		0x1000
#define MDIO_CL73_IEEEB0_CL73_AN_CONTROL_MAIN_RST	0x8000

#define MDIO_REG_BANK_CL73_IEEEB1	0x10
#define MDIO_CL73_IEEEB1_AN_ADV1		0x00
#define MDIO_CL73_IEEEB1_AN_ADV1_PAUSE			0x0400
#define MDIO_CL73_IEEEB1_AN_ADV1_ASYMMETRIC		0x0800
#define MDIO_CL73_IEEEB1_AN_ADV1_PAUSE_BOTH		0x0C00
#define MDIO_CL73_IEEEB1_AN_ADV1_PAUSE_MASK		0x0C00
#define MDIO_CL73_IEEEB1_AN_ADV2		0x01
#define MDIO_CL73_IEEEB1_AN_ADV2_ADVR_1000M		0x0000
#define MDIO_CL73_IEEEB1_AN_ADV2_ADVR_1000M_KX		0x0020
#define MDIO_CL73_IEEEB1_AN_ADV2_ADVR_10G_KX4		0x0040
#define MDIO_CL73_IEEEB1_AN_ADV2_ADVR_10G_KR		0x0080
#define MDIO_CL73_IEEEB1_AN_LP_ADV1		0x03
#define MDIO_CL73_IEEEB1_AN_LP_ADV1_PAUSE		0x0400
#define MDIO_CL73_IEEEB1_AN_LP_ADV1_ASYMMETRIC		0x0800
#define MDIO_CL73_IEEEB1_AN_LP_ADV1_PAUSE_BOTH		0x0C00
#define MDIO_CL73_IEEEB1_AN_LP_ADV1_PAUSE_MASK		0x0C00
#define MDIO_CL73_IEEEB1_AN_LP_ADV2			0x04

#define MDIO_REG_BANK_RX0				0x80b0
#define MDIO_RX0_RX_STATUS				0x10
#define MDIO_RX0_RX_STATUS_SIGDET			0x8000
#define MDIO_RX0_RX_STATUS_RX_SEQ_DONE			0x1000
#define MDIO_RX0_RX_EQ_BOOST				0x1c
#define MDIO_RX0_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
#define MDIO_RX0_RX_EQ_BOOST_OFFSET_CTRL		0x10

#define MDIO_REG_BANK_RX1				0x80c0
#define MDIO_RX1_RX_EQ_BOOST				0x1c
#define MDIO_RX1_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
#define MDIO_RX1_RX_EQ_BOOST_OFFSET_CTRL		0x10

#define MDIO_REG_BANK_RX2				0x80d0
#define MDIO_RX2_RX_EQ_BOOST				0x1c
#define MDIO_RX2_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
#define MDIO_RX2_RX_EQ_BOOST_OFFSET_CTRL		0x10

#define MDIO_REG_BANK_RX3				0x80e0
#define MDIO_RX3_RX_EQ_BOOST				0x1c
#define MDIO_RX3_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
#define MDIO_RX3_RX_EQ_BOOST_OFFSET_CTRL		0x10

#define MDIO_REG_BANK_RX_ALL				0x80f0
#define MDIO_RX_ALL_RX_EQ_BOOST 			0x1c
#define MDIO_RX_ALL_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
#define MDIO_RX_ALL_RX_EQ_BOOST_OFFSET_CTRL	0x10

#define MDIO_REG_BANK_TX0				0x8060
#define MDIO_TX0_TX_DRIVER				0x17
#define MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK		0xf000
#define MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT		12
#define MDIO_TX0_TX_DRIVER_IDRIVER_MASK 		0x0f00
#define MDIO_TX0_TX_DRIVER_IDRIVER_SHIFT		8
#define MDIO_TX0_TX_DRIVER_IPREDRIVER_MASK		0x00f0
#define MDIO_TX0_TX_DRIVER_IPREDRIVER_SHIFT		4
#define MDIO_TX0_TX_DRIVER_IFULLSPD_MASK		0x000e
#define MDIO_TX0_TX_DRIVER_IFULLSPD_SHIFT		1
#define MDIO_TX0_TX_DRIVER_ICBUF1T			1

#define MDIO_REG_BANK_TX1				0x8070
#define MDIO_TX1_TX_DRIVER				0x17
#define MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK		0xf000
#define MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT		12
#define MDIO_TX0_TX_DRIVER_IDRIVER_MASK 		0x0f00
#define MDIO_TX0_TX_DRIVER_IDRIVER_SHIFT		8
#define MDIO_TX0_TX_DRIVER_IPREDRIVER_MASK		0x00f0
#define MDIO_TX0_TX_DRIVER_IPREDRIVER_SHIFT		4
#define MDIO_TX0_TX_DRIVER_IFULLSPD_MASK		0x000e
#define MDIO_TX0_TX_DRIVER_IFULLSPD_SHIFT		1
#define MDIO_TX0_TX_DRIVER_ICBUF1T			1

#define MDIO_REG_BANK_TX2				0x8080
#define MDIO_TX2_TX_DRIVER				0x17
#define MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK		0xf000
#define MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT		12
#define MDIO_TX0_TX_DRIVER_IDRIVER_MASK 		0x0f00
#define MDIO_TX0_TX_DRIVER_IDRIVER_SHIFT		8
#define MDIO_TX0_TX_DRIVER_IPREDRIVER_MASK		0x00f0
#define MDIO_TX0_TX_DRIVER_IPREDRIVER_SHIFT		4
#define MDIO_TX0_TX_DRIVER_IFULLSPD_MASK		0x000e
#define MDIO_TX0_TX_DRIVER_IFULLSPD_SHIFT		1
#define MDIO_TX0_TX_DRIVER_ICBUF1T			1

#define MDIO_REG_BANK_TX3				0x8090
#define MDIO_TX3_TX_DRIVER				0x17
#define MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK		0xf000
#define MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT		12
#define MDIO_TX0_TX_DRIVER_IDRIVER_MASK 		0x0f00
#define MDIO_TX0_TX_DRIVER_IDRIVER_SHIFT		8
#define MDIO_TX0_TX_DRIVER_IPREDRIVER_MASK		0x00f0
#define MDIO_TX0_TX_DRIVER_IPREDRIVER_SHIFT		4
#define MDIO_TX0_TX_DRIVER_IFULLSPD_MASK		0x000e
#define MDIO_TX0_TX_DRIVER_IFULLSPD_SHIFT		1
#define MDIO_TX0_TX_DRIVER_ICBUF1T			1

#define MDIO_REG_BANK_XGXS_BLOCK0			0x8000
#define MDIO_BLOCK0_XGXS_CONTROL			0x10

#define MDIO_REG_BANK_XGXS_BLOCK1			0x8010
#define MDIO_BLOCK1_LANE_CTRL0				0x15
#define MDIO_BLOCK1_LANE_CTRL1				0x16
#define MDIO_BLOCK1_LANE_CTRL2				0x17
#define MDIO_BLOCK1_LANE_PRBS				0x19

#define MDIO_REG_BANK_XGXS_BLOCK2			0x8100
#define MDIO_XGXS_BLOCK2_RX_LN_SWAP			0x10
#define MDIO_XGXS_BLOCK2_RX_LN_SWAP_ENABLE		0x8000
#define MDIO_XGXS_BLOCK2_RX_LN_SWAP_FORCE_ENABLE	0x4000
#define MDIO_XGXS_BLOCK2_TX_LN_SWAP		0x11
#define MDIO_XGXS_BLOCK2_TX_LN_SWAP_ENABLE		0x8000
#define MDIO_XGXS_BLOCK2_UNICORE_MODE_10G	0x14
#define MDIO_XGXS_BLOCK2_UNICORE_MODE_10G_CX4_XGXS	0x0001
#define MDIO_XGXS_BLOCK2_UNICORE_MODE_10G_HIGIG_XGXS	0x0010
#define MDIO_XGXS_BLOCK2_TEST_MODE_LANE 	0x15

#define MDIO_REG_BANK_GP_STATUS 			0x8120
#define MDIO_GP_STATUS_TOP_AN_STATUS1				0x1B
#define MDIO_GP_STATUS_TOP_AN_STATUS1_CL73_AUTONEG_COMPLETE	0x0001
#define MDIO_GP_STATUS_TOP_AN_STATUS1_CL37_AUTONEG_COMPLETE	0x0002
#define MDIO_GP_STATUS_TOP_AN_STATUS1_LINK_STATUS		0x0004
#define MDIO_GP_STATUS_TOP_AN_STATUS1_DUPLEX_STATUS		0x0008
#define MDIO_GP_STATUS_TOP_AN_STATUS1_CL73_MR_LP_NP_AN_ABLE	0x0010
#define MDIO_GP_STATUS_TOP_AN_STATUS1_CL73_LP_NP_BAM_ABLE	0x0020
#define MDIO_GP_STATUS_TOP_AN_STATUS1_PAUSE_RSOLUTION_TXSIDE	0x0040
#define MDIO_GP_STATUS_TOP_AN_STATUS1_PAUSE_RSOLUTION_RXSIDE	0x0080
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_MASK 	0x3f00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10M		0x0000
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_100M 	0x0100
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_1G		0x0200
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_2_5G 	0x0300
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_5G		0x0400
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_6G		0x0500
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_HIG	0x0600
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_CX4	0x0700
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_12G_HIG	0x0800
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_12_5G	0x0900
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_13G		0x0A00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_15G		0x0B00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_16G		0x0C00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_1G_KX	0x0D00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_KX4	0x0E00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_KR	0x0F00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_XFI	0x1B00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_20G_DXGXS	0x1E00
#define MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_SFI	0x1F00


#define MDIO_REG_BANK_10G_PARALLEL_DETECT		0x8130
#define MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_STATUS		0x10
#define MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_STATUS_PD_LINK		0x8000
#define MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL		0x11
#define MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL_PARDET10G_EN	0x1
#define MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_LINK		0x13
#define MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_LINK_CNT		(0xb71<<1)

#define MDIO_REG_BANK_SERDES_DIGITAL			0x8300
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL1			0x10
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_FIBER_MODE 		0x0001
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_TBI_IF			0x0002
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_SIGNAL_DETECT_EN		0x0004
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_INVERT_SIGNAL_DETECT	0x0008
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_AUTODET			0x0010
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_MSTR_MODE			0x0020
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL2			0x11
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL2_PRL_DT_EN			0x0001
#define MDIO_SERDES_DIGITAL_A_1000X_CONTROL2_AN_FST_TMR 		0x0040
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1			0x14
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SGMII			0x0001
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_LINK			0x0002
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_DUPLEX			0x0004
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_MASK			0x0018
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_SHIFT 		3
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_2_5G			0x0018
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_1G			0x0010
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_100M			0x0008
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_10M			0x0000
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS2			0x15
#define MDIO_SERDES_DIGITAL_A_1000X_STATUS2_AN_DISABLED 		0x0002
#define MDIO_SERDES_DIGITAL_MISC1				0x18
#define MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_MASK			0xE000
#define MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_25M			0x0000
#define MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_100M			0x2000
#define MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_125M			0x4000
#define MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_156_25M			0x6000
#define MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_187_5M			0x8000
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_SEL			0x0010
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_MASK			0x000f
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_2_5G			0x0000
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_5G			0x0001
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_6G			0x0002
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_10G_HIG			0x0003
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_10G_CX4			0x0004
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_12G			0x0005
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_12_5G			0x0006
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_13G			0x0007
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_15G			0x0008
#define MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_16G			0x0009

#define MDIO_REG_BANK_OVER_1G				0x8320
#define MDIO_OVER_1G_DIGCTL_3_4 				0x14
#define MDIO_OVER_1G_DIGCTL_3_4_MP_ID_MASK				0xffe0
#define MDIO_OVER_1G_DIGCTL_3_4_MP_ID_SHIFT				5
#define MDIO_OVER_1G_UP1					0x19
#define MDIO_OVER_1G_UP1_2_5G						0x0001
#define MDIO_OVER_1G_UP1_5G						0x0002
#define MDIO_OVER_1G_UP1_6G						0x0004
#define MDIO_OVER_1G_UP1_10G						0x0010
#define MDIO_OVER_1G_UP1_10GH						0x0008
#define MDIO_OVER_1G_UP1_12G						0x0020
#define MDIO_OVER_1G_UP1_12_5G						0x0040
#define MDIO_OVER_1G_UP1_13G						0x0080
#define MDIO_OVER_1G_UP1_15G						0x0100
#define MDIO_OVER_1G_UP1_16G						0x0200
#define MDIO_OVER_1G_UP2					0x1A
#define MDIO_OVER_1G_UP2_IPREDRIVER_MASK				0x0007
#define MDIO_OVER_1G_UP2_IDRIVER_MASK					0x0038
#define MDIO_OVER_1G_UP2_PREEMPHASIS_MASK				0x03C0
#define MDIO_OVER_1G_UP3					0x1B
#define MDIO_OVER_1G_UP3_HIGIG2 					0x0001
#define MDIO_OVER_1G_LP_UP1					0x1C
#define MDIO_OVER_1G_LP_UP2					0x1D
#define MDIO_OVER_1G_LP_UP2_MR_ADV_OVER_1G_MASK 			0x03ff
#define MDIO_OVER_1G_LP_UP2_PREEMPHASIS_MASK				0x0780
#define MDIO_OVER_1G_LP_UP2_PREEMPHASIS_SHIFT				7
#define MDIO_OVER_1G_LP_UP3						0x1E

#define MDIO_REG_BANK_REMOTE_PHY			0x8330
#define MDIO_REMOTE_PHY_MISC_RX_STATUS				0x10
#define MDIO_REMOTE_PHY_MISC_RX_STATUS_CL37_FSM_RECEIVED_OVER1G_MSG	0x0010
#define MDIO_REMOTE_PHY_MISC_RX_STATUS_CL37_FSM_RECEIVED_BRCM_OUI_MSG	0x0600

#define MDIO_REG_BANK_BAM_NEXT_PAGE			0x8350
#define MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL			0x10
#define MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_BAM_MODE			0x0001
#define MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_TETON_AN			0x0002

#define MDIO_REG_BANK_CL73_USERB0		0x8370
#define MDIO_CL73_USERB0_CL73_UCTRL				0x10
#define MDIO_CL73_USERB0_CL73_UCTRL_USTAT1_MUXSEL			0x0002
#define MDIO_CL73_USERB0_CL73_USTAT1				0x11
#define MDIO_CL73_USERB0_CL73_USTAT1_LINK_STATUS_CHECK			0x0100
#define MDIO_CL73_USERB0_CL73_USTAT1_AN_GOOD_CHECK_BAM37		0x0400
#define MDIO_CL73_USERB0_CL73_BAM_CTRL1 			0x12
#define MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_EN				0x8000
#define MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_STATION_MNGR_EN		0x4000
#define MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_NP_AFTER_BP_EN		0x2000
#define MDIO_CL73_USERB0_CL73_BAM_CTRL3 			0x14
#define MDIO_CL73_USERB0_CL73_BAM_CTRL3_USE_CL73_HCD_MR 		0x0001

#define MDIO_REG_BANK_AER_BLOCK 		0xFFD0
#define MDIO_AER_BLOCK_AER_REG					0x1E

#define MDIO_REG_BANK_COMBO_IEEE0		0xFFE0
#define MDIO_COMBO_IEEE0_MII_CONTROL				0x10
#define MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_MASK			0x2040
#define MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_10			0x0000
#define MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_100			0x2000
#define MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_1000			0x0040
#define MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX 			0x0100
#define MDIO_COMBO_IEEO_MII_CONTROL_RESTART_AN				0x0200
#define MDIO_COMBO_IEEO_MII_CONTROL_AN_EN				0x1000
#define MDIO_COMBO_IEEO_MII_CONTROL_LOOPBACK				0x4000
#define MDIO_COMBO_IEEO_MII_CONTROL_RESET				0x8000
#define MDIO_COMBO_IEEE0_MII_STATUS				0x11
#define MDIO_COMBO_IEEE0_MII_STATUS_LINK_PASS				0x0004
#define MDIO_COMBO_IEEE0_MII_STATUS_AUTONEG_COMPLETE			0x0020
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV				0x14
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV_FULL_DUPLEX			0x0020
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV_HALF_DUPLEX			0x0040
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK			0x0180
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_NONE			0x0000
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_SYMMETRIC			0x0080
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC			0x0100
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH			0x0180
#define MDIO_COMBO_IEEE0_AUTO_NEG_ADV_NEXT_PAGE 			0x8000
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1 	0x15
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_NEXT_PAGE	0x8000
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_ACK		0x4000
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_PAUSE_MASK	0x0180
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_PAUSE_NONE	0x0000
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_PAUSE_BOTH	0x0180
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_HALF_DUP_CAP	0x0040
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_FULL_DUP_CAP	0x0020
/*WhenthelinkpartnerisinSGMIImode(bit0=1),then
bit15=link,bit12=duplex,bits11:10=speed,bit14=acknowledge.
Theotherbitsarereservedandshouldbezero*/
#define MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_SGMII_MODE	0x0001


#define MDIO_PMA_DEVAD			0x1
/*ieee*/
#define MDIO_PMA_REG_CTRL		0x0
#define MDIO_PMA_REG_STATUS		0x1
#define MDIO_PMA_REG_10G_CTRL2		0x7
#define MDIO_PMA_REG_TX_DISABLE		0x0009
#define MDIO_PMA_REG_RX_SD		0xa
/*bcm*/
#define MDIO_PMA_REG_BCM_CTRL		0x0096
#define MDIO_PMA_REG_FEC_CTRL		0x00ab
#define MDIO_PMA_REG_PHY_IDENTIFIER	0xc800
#define MDIO_PMA_REG_DIGITAL_CTRL	0xc808
#define MDIO_PMA_REG_DIGITAL_STATUS	0xc809
#define MDIO_PMA_REG_TX_POWER_DOWN	0xca02
#define MDIO_PMA_REG_CMU_PLL_BYPASS	0xca09
#define MDIO_PMA_REG_MISC_CTRL		0xca0a
#define MDIO_PMA_REG_GEN_CTRL		0xca10
#define MDIO_PMA_REG_GEN_CTRL_ROM_RESET_INTERNAL_MP	0x0188
#define MDIO_PMA_REG_GEN_CTRL_ROM_MICRO_RESET		0x018a
#define MDIO_PMA_REG_M8051_MSGIN_REG	0xca12
#define MDIO_PMA_REG_M8051_MSGOUT_REG	0xca13
#define MDIO_PMA_REG_ROM_VER1		0xca19
#define MDIO_PMA_REG_ROM_VER2		0xca1a
#define MDIO_PMA_REG_EDC_FFE_MAIN	0xca1b
#define MDIO_PMA_REG_PLL_BANDWIDTH	0xca1d
#define MDIO_PMA_REG_PLL_CTRL		0xca1e
#define MDIO_PMA_REG_MISC_CTRL0 	0xca23
#define MDIO_PMA_REG_LRM_MODE		0xca3f
#define MDIO_PMA_REG_CDR_BANDWIDTH	0xca46
#define MDIO_PMA_REG_MISC_CTRL1 	0xca85

#define MDIO_PMA_REG_SFP_TWO_WIRE_CTRL		0x8000
#define MDIO_PMA_REG_SFP_TWO_WIRE_CTRL_STATUS_MASK	0x000c
#define MDIO_PMA_REG_SFP_TWO_WIRE_STATUS_IDLE		0x0000
#define MDIO_PMA_REG_SFP_TWO_WIRE_STATUS_COMPLETE	0x0004
#define MDIO_PMA_REG_SFP_TWO_WIRE_STATUS_IN_PROGRESS	0x0008
#define MDIO_PMA_REG_SFP_TWO_WIRE_STATUS_FAILED 	0x000c
#define MDIO_PMA_REG_SFP_TWO_WIRE_BYTE_CNT	0x8002
#define MDIO_PMA_REG_SFP_TWO_WIRE_MEM_ADDR	0x8003
#define MDIO_PMA_REG_8726_TWO_WIRE_DATA_BUF	0xc820
#define MDIO_PMA_REG_8726_TWO_WIRE_DATA_MASK 0xff
#define MDIO_PMA_REG_8726_TX_CTRL1		0xca01
#define MDIO_PMA_REG_8726_TX_CTRL2		0xca05

#define MDIO_PMA_REG_8727_TWO_WIRE_SLAVE_ADDR	0x8005
#define MDIO_PMA_REG_8727_TWO_WIRE_DATA_BUF	0x8007
#define MDIO_PMA_REG_8727_TWO_WIRE_DATA_MASK 0xff
#define MDIO_PMA_REG_8727_TX_CTRL1		0xca02
#define MDIO_PMA_REG_8727_TX_CTRL2		0xca05
#define MDIO_PMA_REG_8727_PCS_OPT_CTRL		0xc808
#define MDIO_PMA_REG_8727_GPIO_CTRL		0xc80e
#define MDIO_PMA_REG_8727_PCS_GP		0xc842
#define MDIO_PMA_REG_8727_OPT_CFG_REG		0xc8e4

#define MDIO_AN_REG_8727_MISC_CTRL		0x8309

#define MDIO_PMA_REG_8073_CHIP_REV			0xc801
#define MDIO_PMA_REG_8073_SPEED_LINK_STATUS		0xc820
#define MDIO_PMA_REG_8073_XAUI_WA			0xc841
#define MDIO_PMA_REG_8073_OPT_DIGITAL_CTRL		0xcd08

#define MDIO_PMA_REG_7101_RESET 	0xc000
#define MDIO_PMA_REG_7107_LED_CNTL	0xc007
#define MDIO_PMA_REG_7107_LINK_LED_CNTL 0xc009
#define MDIO_PMA_REG_7101_VER1		0xc026
#define MDIO_PMA_REG_7101_VER2		0xc027

#define MDIO_PMA_REG_8481_PMD_SIGNAL			0xa811
#define MDIO_PMA_REG_8481_LED1_MASK			0xa82c
#define MDIO_PMA_REG_8481_LED2_MASK			0xa82f
#define MDIO_PMA_REG_8481_LED3_MASK			0xa832
#define MDIO_PMA_REG_8481_LED3_BLINK			0xa834
#define MDIO_PMA_REG_8481_LED5_MASK			0xa838
#define MDIO_PMA_REG_8481_SIGNAL_MASK			0xa835
#define MDIO_PMA_REG_8481_LINK_SIGNAL			0xa83b
#define MDIO_PMA_REG_8481_LINK_SIGNAL_LED4_ENABLE_MASK	0x800
#define MDIO_PMA_REG_8481_LINK_SIGNAL_LED4_ENABLE_SHIFT 11


#define MDIO_WIS_DEVAD			0x2
/*bcm*/
#define MDIO_WIS_REG_LASI_CNTL		0x9002
#define MDIO_WIS_REG_LASI_STATUS	0x9005

#define MDIO_PCS_DEVAD			0x3
#define MDIO_PCS_REG_STATUS		0x0020
#define MDIO_PCS_REG_LASI_STATUS	0x9005
#define MDIO_PCS_REG_7101_DSP_ACCESS	0xD000
#define MDIO_PCS_REG_7101_SPI_MUX	0xD008
#define MDIO_PCS_REG_7101_SPI_CTRL_ADDR 0xE12A
#define MDIO_PCS_REG_7101_SPI_RESET_BIT (5)
#define MDIO_PCS_REG_7101_SPI_FIFO_ADDR 0xE02A
#define MDIO_PCS_REG_7101_SPI_FIFO_ADDR_WRITE_ENABLE_CMD (6)
#define MDIO_PCS_REG_7101_SPI_FIFO_ADDR_BULK_ERASE_CMD	 (0xC7)
#define MDIO_PCS_REG_7101_SPI_FIFO_ADDR_PAGE_PROGRAM_CMD (2)
#define MDIO_PCS_REG_7101_SPI_BYTES_TO_TRANSFER_ADDR 0xE028


#define MDIO_XS_DEVAD			0x4
#define MDIO_XS_PLL_SEQUENCER		0x8000
#define MDIO_XS_SFX7101_XGXS_TEST1	0xc00a

#define MDIO_XS_8706_REG_BANK_RX0	0x80bc
#define MDIO_XS_8706_REG_BANK_RX1	0x80cc
#define MDIO_XS_8706_REG_BANK_RX2	0x80dc
#define MDIO_XS_8706_REG_BANK_RX3	0x80ec
#define MDIO_XS_8706_REG_BANK_RXA	0x80fc

#define MDIO_XS_REG_8073_RX_CTRL_PCIE	0x80FA

#define MDIO_AN_DEVAD			0x7
/*ieee*/
#define MDIO_AN_REG_CTRL		0x0000
#define MDIO_AN_REG_STATUS		0x0001
#define MDIO_AN_REG_STATUS_AN_COMPLETE		0x0020
#define MDIO_AN_REG_ADV_PAUSE		0x0010
#define MDIO_AN_REG_ADV_PAUSE_PAUSE		0x0400
#define MDIO_AN_REG_ADV_PAUSE_ASYMMETRIC	0x0800
#define MDIO_AN_REG_ADV_PAUSE_BOTH		0x0C00
#define MDIO_AN_REG_ADV_PAUSE_MASK		0x0C00
#define MDIO_AN_REG_ADV 		0x0011
#define MDIO_AN_REG_ADV2		0x0012
#define MDIO_AN_REG_LP_AUTO_NEG		0x0013
#define MDIO_AN_REG_LP_AUTO_NEG2	0x0014
#define MDIO_AN_REG_MASTER_STATUS	0x0021
/*bcm*/
#define MDIO_AN_REG_LINK_STATUS 	0x8304
#define MDIO_AN_REG_CL37_CL73		0x8370
#define MDIO_AN_REG_CL37_AN		0xffe0
#define MDIO_AN_REG_CL37_FC_LD		0xffe4
#define		MDIO_AN_REG_CL37_FC_LP		0xffe5
#define		MDIO_AN_REG_1000T_STATUS	0xffea

#define MDIO_AN_REG_8073_2_5G		0x8329
#define MDIO_AN_REG_8073_BAM		0x8350

#define MDIO_AN_REG_8481_10GBASE_T_AN_CTRL	0x0020
#define MDIO_AN_REG_8481_LEGACY_MII_CTRL	0xffe0
#define MDIO_AN_REG_8481_MII_CTRL_FORCE_1G	0x40
#define MDIO_AN_REG_8481_LEGACY_MII_STATUS	0xffe1
#define MDIO_AN_REG_8481_LEGACY_AN_ADV		0xffe4
#define MDIO_AN_REG_8481_LEGACY_AN_EXPANSION	0xffe6
#define MDIO_AN_REG_8481_1000T_CTRL		0xffe9
#define MDIO_AN_REG_8481_1G_100T_EXT_CTRL	0xfff0
#define MIDO_AN_REG_8481_EXT_CTRL_FORCE_LEDS_OFF	0x0008
#define MDIO_AN_REG_8481_EXPANSION_REG_RD_RW	0xfff5
#define MDIO_AN_REG_8481_EXPANSION_REG_ACCESS	0xfff7
#define MDIO_AN_REG_8481_AUX_CTRL		0xfff8
#define MDIO_AN_REG_8481_LEGACY_SHADOW		0xfffc

/* BCM84823 only */
#define MDIO_CTL_DEVAD			0x1e
#define MDIO_CTL_REG_84823_MEDIA		0x401a
#define MDIO_CTL_REG_84823_MEDIA_MAC_MASK		0x0018
	/* These pins configure the BCM84823 interface to MAC after reset. */
#define MDIO_CTL_REG_84823_CTRL_MAC_XFI			0x0008
#define MDIO_CTL_REG_84823_MEDIA_MAC_XAUI_M		0x0010
	/* These pins configure the BCM84823 interface to Line after reset. */
#define MDIO_CTL_REG_84823_MEDIA_LINE_MASK		0x0060
#define MDIO_CTL_REG_84823_MEDIA_LINE_XAUI_L		0x0020
#define MDIO_CTL_REG_84823_MEDIA_LINE_XFI		0x0040
	/* When this pin is active high during reset, 10GBASE-T core is power
	 * down, When it is active low the 10GBASE-T is power up
	 */
#define MDIO_CTL_REG_84823_MEDIA_COPPER_CORE_DOWN	0x0080
#define MDIO_CTL_REG_84823_MEDIA_PRIORITY_MASK		0x0100
#define MDIO_CTL_REG_84823_MEDIA_PRIORITY_COPPER	0x0000
#define MDIO_CTL_REG_84823_MEDIA_PRIORITY_FIBER		0x0100
#define MDIO_CTL_REG_84823_MEDIA_FIBER_1G			0x1000
#define MDIO_CTL_REG_84823_USER_CTRL_REG			0x4005
#define MDIO_CTL_REG_84823_USER_CTRL_CMS			0x0080
#define MDIO_PMA_REG_84823_CTL_SLOW_CLK_CNT_HIGH		0xa82b
#define MDIO_PMA_REG_84823_BLINK_RATE_VAL_15P9HZ	0x2f
#define MDIO_PMA_REG_84823_CTL_LED_CTL_1			0xa8e3
#define MDIO_PMA_REG_84833_CTL_LED_CTL_1			0xa8ec
#define MDIO_PMA_REG_84823_LED3_STRETCH_EN			0x0080

/* BCM84833 only */
#define MDIO_84833_TOP_CFG_XGPHY_STRAP1			0x401a
#define MDIO_84833_SUPER_ISOLATE		0x8000
/* These are mailbox register set used by 84833. */
#define MDIO_84833_TOP_CFG_SCRATCH_REG0			0x4005
#define MDIO_84833_TOP_CFG_SCRATCH_REG1			0x4006
#define MDIO_84833_TOP_CFG_SCRATCH_REG2			0x4007
#define MDIO_84833_TOP_CFG_SCRATCH_REG3			0x4008
#define MDIO_84833_TOP_CFG_SCRATCH_REG4			0x4009
#define MDIO_84833_TOP_CFG_SCRATCH_REG26		0x4037
#define MDIO_84833_TOP_CFG_SCRATCH_REG27		0x4038
#define MDIO_84833_TOP_CFG_SCRATCH_REG28		0x4039
#define MDIO_84833_TOP_CFG_SCRATCH_REG29		0x403a
#define MDIO_84833_TOP_CFG_SCRATCH_REG30		0x403b
#define MDIO_84833_TOP_CFG_SCRATCH_REG31		0x403c
#define MDIO_84833_CMD_HDLR_COMMAND	MDIO_84833_TOP_CFG_SCRATCH_REG0
#define MDIO_84833_CMD_HDLR_STATUS	MDIO_84833_TOP_CFG_SCRATCH_REG26
#define MDIO_84833_CMD_HDLR_DATA1	MDIO_84833_TOP_CFG_SCRATCH_REG27
#define MDIO_84833_CMD_HDLR_DATA2	MDIO_84833_TOP_CFG_SCRATCH_REG28
#define MDIO_84833_CMD_HDLR_DATA3	MDIO_84833_TOP_CFG_SCRATCH_REG29
#define MDIO_84833_CMD_HDLR_DATA4	MDIO_84833_TOP_CFG_SCRATCH_REG30
#define MDIO_84833_CMD_HDLR_DATA5	MDIO_84833_TOP_CFG_SCRATCH_REG31

/* Mailbox command set used by 84833. */
#define PHY84833_CMD_SET_PAIR_SWAP			0x8001
#define PHY84833_CMD_GET_EEE_MODE			0x8008
#define PHY84833_CMD_SET_EEE_MODE			0x8009
/* Mailbox status set used by 84833. */
#define PHY84833_STATUS_CMD_RECEIVED			0x0001
#define PHY84833_STATUS_CMD_IN_PROGRESS			0x0002
#define PHY84833_STATUS_CMD_COMPLETE_PASS		0x0004
#define PHY84833_STATUS_CMD_COMPLETE_ERROR		0x0008
#define PHY84833_STATUS_CMD_OPEN_FOR_CMDS		0x0010
#define PHY84833_STATUS_CMD_SYSTEM_BOOT			0x0020
#define PHY84833_STATUS_CMD_NOT_OPEN_FOR_CMDS		0x0040
#define PHY84833_STATUS_CMD_CLEAR_COMPLETE		0x0080
#define PHY84833_STATUS_CMD_OPEN_OVERRIDE		0xa5a5


/* Warpcore clause 45 addressing */
#define MDIO_WC_DEVAD					0x3
#define MDIO_WC_REG_IEEE0BLK_MIICNTL			0x0
#define MDIO_WC_REG_IEEE0BLK_AUTONEGNP			0x7
#define MDIO_WC_REG_AN_IEEE1BLK_AN_ADVERTISEMENT0	0x10
#define MDIO_WC_REG_AN_IEEE1BLK_AN_ADVERTISEMENT1	0x11
#define MDIO_WC_REG_AN_IEEE1BLK_AN_ADVERTISEMENT2	0x12
#define MDIO_WC_REG_AN_IEEE1BLK_AN_ADV2_FEC_ABILITY	0x4000
#define MDIO_WC_REG_AN_IEEE1BLK_AN_ADV2_FEC_REQ		0x8000
#define MDIO_WC_REG_PMD_IEEE9BLK_TENGBASE_KR_PMD_CONTROL_REGISTER_150  0x96
#define MDIO_WC_REG_XGXSBLK0_XGXSCONTROL		0x8000
#define MDIO_WC_REG_XGXSBLK0_MISCCONTROL1		0x800e
#define MDIO_WC_REG_XGXSBLK1_DESKEW			0x8010
#define MDIO_WC_REG_XGXSBLK1_LANECTRL0			0x8015
#define MDIO_WC_REG_XGXSBLK1_LANECTRL1			0x8016
#define MDIO_WC_REG_XGXSBLK1_LANECTRL2			0x8017
#define MDIO_WC_REG_TX0_ANA_CTRL0			0x8061
#define MDIO_WC_REG_TX1_ANA_CTRL0			0x8071
#define MDIO_WC_REG_TX2_ANA_CTRL0			0x8081
#define MDIO_WC_REG_TX3_ANA_CTRL0			0x8091
#define MDIO_WC_REG_TX0_TX_DRIVER			0x8067
#define MDIO_WC_REG_TX0_TX_DRIVER_IPRE_DRIVER_OFFSET		0x04
#define MDIO_WC_REG_TX0_TX_DRIVER_IPRE_DRIVER_MASK			0x00f0
#define MDIO_WC_REG_TX0_TX_DRIVER_IDRIVER_OFFSET		0x08
#define MDIO_WC_REG_TX0_TX_DRIVER_IDRIVER_MASK				0x0f00
#define MDIO_WC_REG_TX0_TX_DRIVER_POST2_COEFF_OFFSET		0x0c
#define MDIO_WC_REG_TX0_TX_DRIVER_POST2_COEFF_MASK			0x7000
#define MDIO_WC_REG_TX1_TX_DRIVER			0x8077
#define MDIO_WC_REG_TX2_TX_DRIVER			0x8087
#define MDIO_WC_REG_TX3_TX_DRIVER			0x8097
#define MDIO_WC_REG_RX0_ANARXCONTROL1G			0x80b9
#define MDIO_WC_REG_RX2_ANARXCONTROL1G			0x80d9
#define MDIO_WC_REG_RX0_PCI_CTRL			0x80ba
#define MDIO_WC_REG_RX1_PCI_CTRL			0x80ca
#define MDIO_WC_REG_RX2_PCI_CTRL			0x80da
#define MDIO_WC_REG_RX3_PCI_CTRL			0x80ea
#define MDIO_WC_REG_XGXSBLK2_UNICORE_MODE_10G		0x8104
#define MDIO_WC_REG_XGXS_STATUS3			0x8129
#define MDIO_WC_REG_PAR_DET_10G_STATUS			0x8130
#define MDIO_WC_REG_PAR_DET_10G_CTRL			0x8131
#define MDIO_WC_REG_XGXS_X2_CONTROL2			0x8141
#define MDIO_WC_REG_XGXS_RX_LN_SWAP1			0x816B
#define MDIO_WC_REG_XGXS_TX_LN_SWAP1			0x8169
#define MDIO_WC_REG_GP2_STATUS_GP_2_0			0x81d0
#define MDIO_WC_REG_GP2_STATUS_GP_2_1			0x81d1
#define MDIO_WC_REG_GP2_STATUS_GP_2_2			0x81d2
#define MDIO_WC_REG_GP2_STATUS_GP_2_3			0x81d3
#define MDIO_WC_REG_GP2_STATUS_GP_2_4			0x81d4
#define MDIO_WC_REG_GP2_STATUS_GP_2_4_CL73_AN_CMPL 0x1000
#define MDIO_WC_REG_GP2_STATUS_GP_2_4_CL37_AN_CMPL 0x0100
#define MDIO_WC_REG_GP2_STATUS_GP_2_4_CL37_LP_AN_CAP 0x0010
#define MDIO_WC_REG_GP2_STATUS_GP_2_4_CL37_AN_CAP 0x1
#define MDIO_WC_REG_UC_INFO_B0_DEAD_TRAP		0x81EE
#define MDIO_WC_REG_UC_INFO_B1_VERSION			0x81F0
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_MODE		0x81F2
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_LANE0_OFFSET	0x0
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_MODE_DEFAULT	    0x0
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_MODE_SFP_OPT_LR	    0x1
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_MODE_SFP_DAC	    0x2
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_MODE_SFP_XLAUI	    0x3
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_MODE_LONG_CH_6G	    0x4
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_LANE1_OFFSET	0x4
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_LANE2_OFFSET	0x8
#define MDIO_WC_REG_UC_INFO_B1_FIRMWARE_LANE3_OFFSET	0xc
#define MDIO_WC_REG_UC_INFO_B1_CRC			0x81FE
#define MDIO_WC_REG_DSC_SMC				0x8213
#define MDIO_WC_REG_DSC2B0_DSC_MISC_CTRL0		0x821e
#define MDIO_WC_REG_TX_FIR_TAP				0x82e2
#define MDIO_WC_REG_TX_FIR_TAP_PRE_TAP_OFFSET		0x00
#define MDIO_WC_REG_TX_FIR_TAP_PRE_TAP_MASK			0x000f
#define MDIO_WC_REG_TX_FIR_TAP_MAIN_TAP_OFFSET		0x04
#define MDIO_WC_REG_TX_FIR_TAP_MAIN_TAP_MASK		0x03f0
#define MDIO_WC_REG_TX_FIR_TAP_POST_TAP_OFFSET		0x0a
#define MDIO_WC_REG_TX_FIR_TAP_POST_TAP_MASK		0x7c00
#define MDIO_WC_REG_TX_FIR_TAP_ENABLE		0x8000
#define MDIO_WC_REG_CL72_USERB0_CL72_MISC1_CONTROL	0x82e3
#define MDIO_WC_REG_CL72_USERB0_CL72_OS_DEF_CTRL	0x82e6
#define MDIO_WC_REG_CL72_USERB0_CL72_BR_DEF_CTRL	0x82e7
#define MDIO_WC_REG_CL72_USERB0_CL72_2P5_DEF_CTRL	0x82e8
#define MDIO_WC_REG_CL72_USERB0_CL72_MISC4_CONTROL	0x82ec
#define MDIO_WC_REG_SERDESDIGITAL_CONTROL1000X1		0x8300
#define MDIO_WC_REG_SERDESDIGITAL_CONTROL1000X2		0x8301
#define MDIO_WC_REG_SERDESDIGITAL_CONTROL1000X3		0x8302
#define MDIO_WC_REG_SERDESDIGITAL_STATUS1000X1		0x8304
#define MDIO_WC_REG_SERDESDIGITAL_MISC1			0x8308
#define MDIO_WC_REG_SERDESDIGITAL_MISC2			0x8309
#define MDIO_WC_REG_DIGITAL3_UP1			0x8329
#define MDIO_WC_REG_DIGITAL3_LP_UP1			 0x832c
#define MDIO_WC_REG_DIGITAL4_MISC3			0x833c
#define MDIO_WC_REG_DIGITAL5_MISC6			0x8345
#define MDIO_WC_REG_DIGITAL5_MISC7			0x8349
#define MDIO_WC_REG_DIGITAL5_ACTUAL_SPEED		0x834e
#define MDIO_WC_REG_DIGITAL6_MP5_NEXTPAGECTRL		0x8350
#define MDIO_WC_REG_CL49_USERB0_CTRL			0x8368
#define MDIO_WC_REG_TX66_CONTROL			0x83b0
#define MDIO_WC_REG_RX66_CONTROL			0x83c0
#define MDIO_WC_REG_RX66_SCW0				0x83c2
#define MDIO_WC_REG_RX66_SCW1				0x83c3
#define MDIO_WC_REG_RX66_SCW2				0x83c4
#define MDIO_WC_REG_RX66_SCW3				0x83c5
#define MDIO_WC_REG_RX66_SCW0_MASK			0x83c6
#define MDIO_WC_REG_RX66_SCW1_MASK			0x83c7
#define MDIO_WC_REG_RX66_SCW2_MASK			0x83c8
#define MDIO_WC_REG_RX66_SCW3_MASK			0x83c9
#define MDIO_WC_REG_FX100_CTRL1				0x8400
#define MDIO_WC_REG_FX100_CTRL3				0x8402

#define MDIO_WC_REG_MICROBLK_CMD			0xffc2
#define MDIO_WC_REG_MICROBLK_DL_STATUS			0xffc5
#define MDIO_WC_REG_MICROBLK_CMD3			0xffcc

#define MDIO_WC_REG_AERBLK_AER				0xffde
#define MDIO_WC_REG_COMBO_IEEE0_MIICTRL			0xffe0
#define MDIO_WC_REG_COMBO_IEEE0_MIIISTAT		0xffe1

#define MDIO_WC0_XGXS_BLK2_LANE_RESET			0x810A
#define MDIO_WC0_XGXS_BLK2_LANE_RESET_RX_BITSHIFT	0
#define MDIO_WC0_XGXS_BLK2_LANE_RESET_TX_BITSHIFT	4

#define MDIO_WC0_XGXS_BLK6_XGXS_X2_CONTROL2		0x8141

#define DIGITAL5_ACTUAL_SPEED_TX_MASK			0x003f

/* 54618se */
#define MDIO_REG_GPHY_PHYID_LSB				0x3
#define MDIO_REG_GPHY_ID_54618SE		0x5cd5
#define MDIO_REG_GPHY_CL45_ADDR_REG			0xd
#define MDIO_REG_GPHY_CL45_DATA_REG			0xe
#define MDIO_REG_GPHY_EEE_ADV			0x3c
#define MDIO_REG_GPHY_EEE_1G		(0x1 << 2)
#define MDIO_REG_GPHY_EEE_100		(0x1 << 1)
#define MDIO_REG_GPHY_EEE_RESOLVED		0x803e
#define MDIO_REG_INTR_STATUS				0x1a
#define MDIO_REG_INTR_MASK				0x1b
#define MDIO_REG_INTR_MASK_LINK_STATUS			(0x1 << 1)
#define MDIO_REG_GPHY_SHADOW				0x1c
#define MDIO_REG_GPHY_SHADOW_LED_SEL1			(0x0d << 10)
#define MDIO_REG_GPHY_SHADOW_LED_SEL2			(0x0e << 10)
#define MDIO_REG_GPHY_SHADOW_WR_ENA			(0x1 << 15)
#define MDIO_REG_GPHY_SHADOW_AUTO_DET_MED		(0x1e << 10)
#define MDIO_REG_GPHY_SHADOW_INVERT_FIB_SD		(0x1 << 8)

#define IGU_FUNC_BASE			0x0400

#define IGU_ADDR_MSIX			0x0000
#define IGU_ADDR_INT_ACK		0x0200
#define IGU_ADDR_PROD_UPD		0x0201
#define IGU_ADDR_ATTN_BITS_UPD	0x0202
#define IGU_ADDR_ATTN_BITS_SET	0x0203
#define IGU_ADDR_ATTN_BITS_CLR	0x0204
#define IGU_ADDR_COALESCE_NOW	0x0205
#define IGU_ADDR_SIMD_MASK		0x0206
#define IGU_ADDR_SIMD_NOMASK	0x0207
#define IGU_ADDR_MSI_CTL		0x0210
#define IGU_ADDR_MSI_ADDR_LO	0x0211
#define IGU_ADDR_MSI_ADDR_HI	0x0212
#define IGU_ADDR_MSI_DATA		0x0213

#define IGU_USE_REGISTER_ustorm_type_0_sb_cleanup  0
#define IGU_USE_REGISTER_ustorm_type_1_sb_cleanup  1
#define IGU_USE_REGISTER_cstorm_type_0_sb_cleanup  2
#define IGU_USE_REGISTER_cstorm_type_1_sb_cleanup  3

#define COMMAND_REG_INT_ACK	    0x0
#define COMMAND_REG_PROD_UPD	    0x4
#define COMMAND_REG_ATTN_BITS_UPD   0x8
#define COMMAND_REG_ATTN_BITS_SET   0xc
#define COMMAND_REG_ATTN_BITS_CLR   0x10
#define COMMAND_REG_COALESCE_NOW    0x14
#define COMMAND_REG_SIMD_MASK	    0x18
#define COMMAND_REG_SIMD_NOMASK     0x1c


#define IGU_MEM_BASE						0x0000

#define IGU_MEM_MSIX_BASE					0x0000
#define IGU_MEM_MSIX_UPPER					0x007f
#define IGU_MEM_MSIX_RESERVED_UPPER			0x01ff

#define IGU_MEM_PBA_MSIX_BASE				0x0200
#define IGU_MEM_PBA_MSIX_UPPER				0x0200

#define IGU_CMD_BACKWARD_COMP_PROD_UPD		0x0201
#define IGU_MEM_PBA_MSIX_RESERVED_UPPER 	0x03ff

#define IGU_CMD_INT_ACK_BASE				0x0400
#define IGU_CMD_INT_ACK_UPPER\
	(IGU_CMD_INT_ACK_BASE + MAX_SB_PER_PORT * NUM_OF_PORTS_PER_PATH - 1)
#define IGU_CMD_INT_ACK_RESERVED_UPPER		0x04ff

#define IGU_CMD_E2_PROD_UPD_BASE			0x0500
#define IGU_CMD_E2_PROD_UPD_UPPER\
	(IGU_CMD_E2_PROD_UPD_BASE + MAX_SB_PER_PORT * NUM_OF_PORTS_PER_PATH - 1)
#define IGU_CMD_E2_PROD_UPD_RESERVED_UPPER	0x059f

#define IGU_CMD_ATTN_BIT_UPD_UPPER			0x05a0
#define IGU_CMD_ATTN_BIT_SET_UPPER			0x05a1
#define IGU_CMD_ATTN_BIT_CLR_UPPER			0x05a2

#define IGU_REG_SISR_MDPC_WMASK_UPPER		0x05a3
#define IGU_REG_SISR_MDPC_WMASK_LSB_UPPER	0x05a4
#define IGU_REG_SISR_MDPC_WMASK_MSB_UPPER	0x05a5
#define IGU_REG_SISR_MDPC_WOMASK_UPPER		0x05a6

#define IGU_REG_RESERVED_UPPER				0x05ff
/* Fields of IGU PF CONFIGRATION REGISTER */
#define IGU_PF_CONF_FUNC_EN	  (0x1<<0)  /* function enable	      */
#define IGU_PF_CONF_MSI_MSIX_EN   (0x1<<1)  /* MSI/MSIX enable	      */
#define IGU_PF_CONF_INT_LINE_EN   (0x1<<2)  /* INT enable	      */
#define IGU_PF_CONF_ATTN_BIT_EN   (0x1<<3)  /* attention enable       */
#define IGU_PF_CONF_SINGLE_ISR_EN (0x1<<4)  /* single ISR mode enable */
#define IGU_PF_CONF_SIMD_MODE	  (0x1<<5)  /* simd all ones mode     */

/* Fields of IGU VF CONFIGRATION REGISTER */
#define IGU_VF_CONF_FUNC_EN	   (0x1<<0)  /* function enable        */
#define IGU_VF_CONF_MSI_MSIX_EN    (0x1<<1)  /* MSI/MSIX enable        */
#define IGU_VF_CONF_PARENT_MASK    (0x3<<2)  /* Parent PF	       */
#define IGU_VF_CONF_PARENT_SHIFT   2	     /* Parent PF	       */
#define IGU_VF_CONF_SINGLE_ISR_EN  (0x1<<4)  /* single ISR mode enable */


#define IGU_BC_DSB_NUM_SEGS    5
#define IGU_BC_NDSB_NUM_SEGS   2
#define IGU_NORM_DSB_NUM_SEGS  2
#define IGU_NORM_NDSB_NUM_SEGS 1
#define IGU_BC_BASE_DSB_PROD   128
#define IGU_NORM_BASE_DSB_PROD 136

	/* FID (if VF - [6] = 0; [5:0] = VF number; if PF - [6] = 1; \
	[5:2] = 0; [1:0] = PF number) */
#define IGU_FID_ENCODE_IS_PF	    (0x1<<6)
#define IGU_FID_ENCODE_IS_PF_SHIFT  6
#define IGU_FID_VF_NUM_MASK	    (0x3f)
#define IGU_FID_PF_NUM_MASK	    (0x7)

#define IGU_REG_MAPPING_MEMORY_VALID		(1<<0)
#define IGU_REG_MAPPING_MEMORY_VECTOR_MASK	(0x3F<<1)
#define IGU_REG_MAPPING_MEMORY_VECTOR_SHIFT	1
#define IGU_REG_MAPPING_MEMORY_FID_MASK	(0x7F<<7)
#define IGU_REG_MAPPING_MEMORY_FID_SHIFT	7


#define CDU_REGION_NUMBER_XCM_AG 2
#define CDU_REGION_NUMBER_UCM_AG 4


/**
 * String-to-compress [31:8] = CID (all 24 bits)
 * String-to-compress [7:4] = Region
 * String-to-compress [3:0] = Type
 */
#define CDU_VALID_DATA(_cid, _region, _type)\
	(((_cid) << 8) | (((_region)&0xf)<<4) | (((_type)&0xf)))
#define CDU_CRC8(_cid, _region, _type)\
	(calc_crc8(CDU_VALID_DATA(_cid, _region, _type), 0xff))
#define CDU_RSRVD_VALUE_TYPE_A(_cid, _region, _type)\
	(0x80 | ((CDU_CRC8(_cid, _region, _type)) & 0x7f))
#define CDU_RSRVD_VALUE_TYPE_B(_crc, _type)\
	(0x80 | ((_type)&0xf << 3) | ((CDU_CRC8(_cid, _region, _type)) & 0x7))
#define CDU_RSRVD_INVALIDATE_CONTEXT_VALUE(_val) ((_val) & ~0x80)

/******************************************************************************
 * Description:
 *	   Calculates crc 8 on a word value: polynomial 0-1-2-8
 *	   Code was translated from Verilog.
 * Return:
 *****************************************************************************/
static inline u8 calc_crc8(u32 data, u8 crc)
{
	u8 D[32];
	u8 NewCRC[8];
	u8 C[8];
	u8 crc_res;
	u8 i;

	/* split the data into 31 bits */
	for (i = 0; i < 32; i++) {
		D[i] = (u8)(data & 1);
		data = data >> 1;
	}

	/* split the crc into 8 bits */
	for (i = 0; i < 8; i++) {
		C[i] = crc & 1;
		crc = crc >> 1;
	}

	NewCRC[0] = D[31] ^ D[30] ^ D[28] ^ D[23] ^ D[21] ^ D[19] ^ D[18] ^
		    D[16] ^ D[14] ^ D[12] ^ D[8] ^ D[7] ^ D[6] ^ D[0] ^ C[4] ^
		    C[6] ^ C[7];
	NewCRC[1] = D[30] ^ D[29] ^ D[28] ^ D[24] ^ D[23] ^ D[22] ^ D[21] ^
		    D[20] ^ D[18] ^ D[17] ^ D[16] ^ D[15] ^ D[14] ^ D[13] ^
		    D[12] ^ D[9] ^ D[6] ^ D[1] ^ D[0] ^ C[0] ^ C[4] ^ C[5] ^
		    C[6];
	NewCRC[2] = D[29] ^ D[28] ^ D[25] ^ D[24] ^ D[22] ^ D[17] ^ D[15] ^
		    D[13] ^ D[12] ^ D[10] ^ D[8] ^ D[6] ^ D[2] ^ D[1] ^ D[0] ^
		    C[0] ^ C[1] ^ C[4] ^ C[5];
	NewCRC[3] = D[30] ^ D[29] ^ D[26] ^ D[25] ^ D[23] ^ D[18] ^ D[16] ^
		    D[14] ^ D[13] ^ D[11] ^ D[9] ^ D[7] ^ D[3] ^ D[2] ^ D[1] ^
		    C[1] ^ C[2] ^ C[5] ^ C[6];
	NewCRC[4] = D[31] ^ D[30] ^ D[27] ^ D[26] ^ D[24] ^ D[19] ^ D[17] ^
		    D[15] ^ D[14] ^ D[12] ^ D[10] ^ D[8] ^ D[4] ^ D[3] ^ D[2] ^
		    C[0] ^ C[2] ^ C[3] ^ C[6] ^ C[7];
	NewCRC[5] = D[31] ^ D[28] ^ D[27] ^ D[25] ^ D[20] ^ D[18] ^ D[16] ^
		    D[15] ^ D[13] ^ D[11] ^ D[9] ^ D[5] ^ D[4] ^ D[3] ^ C[1] ^
		    C[3] ^ C[4] ^ C[7];
	NewCRC[6] = D[29] ^ D[28] ^ D[26] ^ D[21] ^ D[19] ^ D[17] ^ D[16] ^
		    D[14] ^ D[12] ^ D[10] ^ D[6] ^ D[5] ^ D[4] ^ C[2] ^ C[4] ^
		    C[5];
	NewCRC[7] = D[30] ^ D[29] ^ D[27] ^ D[22] ^ D[20] ^ D[18] ^ D[17] ^
		    D[15] ^ D[13] ^ D[11] ^ D[7] ^ D[6] ^ D[5] ^ C[3] ^ C[5] ^
		    C[6];

	crc_res = 0;
	for (i = 0; i < 8; i++)
		crc_res |= (NewCRC[i] << i);

	return crc_res;
}


#endif /* BNX2X_REG_H */
