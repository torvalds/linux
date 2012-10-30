#ifndef __NOUVEAU_GRAPH_REGS_H__
#define __NOUVEAU_GRAPH_REGS_H__

#define NV04_PGRAPH_DEBUG_0                                0x00400080
#define NV04_PGRAPH_DEBUG_1                                0x00400084
#define NV04_PGRAPH_DEBUG_2                                0x00400088
#define NV04_PGRAPH_DEBUG_3                                0x0040008c
#define NV10_PGRAPH_DEBUG_4                                0x00400090
#define NV03_PGRAPH_INTR                                   0x00400100
#define NV03_PGRAPH_NSTATUS                                0x00400104
#    define NV04_PGRAPH_NSTATUS_STATE_IN_USE                  (1<<11)
#    define NV04_PGRAPH_NSTATUS_INVALID_STATE                 (1<<12)
#    define NV04_PGRAPH_NSTATUS_BAD_ARGUMENT                  (1<<13)
#    define NV04_PGRAPH_NSTATUS_PROTECTION_FAULT              (1<<14)
#    define NV10_PGRAPH_NSTATUS_STATE_IN_USE                  (1<<23)
#    define NV10_PGRAPH_NSTATUS_INVALID_STATE                 (1<<24)
#    define NV10_PGRAPH_NSTATUS_BAD_ARGUMENT                  (1<<25)
#    define NV10_PGRAPH_NSTATUS_PROTECTION_FAULT              (1<<26)
#define NV03_PGRAPH_NSOURCE                                0x00400108
#    define NV03_PGRAPH_NSOURCE_NOTIFICATION                   (1<<0)
#    define NV03_PGRAPH_NSOURCE_DATA_ERROR                     (1<<1)
#    define NV03_PGRAPH_NSOURCE_PROTECTION_ERROR               (1<<2)
#    define NV03_PGRAPH_NSOURCE_RANGE_EXCEPTION                (1<<3)
#    define NV03_PGRAPH_NSOURCE_LIMIT_COLOR                    (1<<4)
#    define NV03_PGRAPH_NSOURCE_LIMIT_ZETA                     (1<<5)
#    define NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD                   (1<<6)
#    define NV03_PGRAPH_NSOURCE_DMA_R_PROTECTION               (1<<7)
#    define NV03_PGRAPH_NSOURCE_DMA_W_PROTECTION               (1<<8)
#    define NV03_PGRAPH_NSOURCE_FORMAT_EXCEPTION               (1<<9)
#    define NV03_PGRAPH_NSOURCE_PATCH_EXCEPTION               (1<<10)
#    define NV03_PGRAPH_NSOURCE_STATE_INVALID                 (1<<11)
#    define NV03_PGRAPH_NSOURCE_DOUBLE_NOTIFY                 (1<<12)
#    define NV03_PGRAPH_NSOURCE_NOTIFY_IN_USE                 (1<<13)
#    define NV03_PGRAPH_NSOURCE_METHOD_CNT                    (1<<14)
#    define NV03_PGRAPH_NSOURCE_BFR_NOTIFICATION              (1<<15)
#    define NV03_PGRAPH_NSOURCE_DMA_VTX_PROTECTION            (1<<16)
#    define NV03_PGRAPH_NSOURCE_DMA_WIDTH_A                   (1<<17)
#    define NV03_PGRAPH_NSOURCE_DMA_WIDTH_B                   (1<<18)
#define NV03_PGRAPH_INTR_EN                                0x00400140
#define NV40_PGRAPH_INTR_EN                                0x0040013C
#    define NV_PGRAPH_INTR_NOTIFY                              (1<<0)
#    define NV_PGRAPH_INTR_MISSING_HW                          (1<<4)
#    define NV_PGRAPH_INTR_CONTEXT_SWITCH                     (1<<12)
#    define NV_PGRAPH_INTR_BUFFER_NOTIFY                      (1<<16)
#    define NV_PGRAPH_INTR_ERROR                              (1<<20)
#define NV10_PGRAPH_CTX_CONTROL                            0x00400144
#define NV10_PGRAPH_CTX_USER                               0x00400148
#define NV10_PGRAPH_CTX_SWITCH(i)                         (0x0040014C + 0x4*(i))
#define NV04_PGRAPH_CTX_SWITCH1                            0x00400160
#define NV10_PGRAPH_CTX_CACHE(i, j)                       (0x00400160	\
							   + 0x4*(i) + 0x20*(j))
#define NV04_PGRAPH_CTX_SWITCH2                            0x00400164
#define NV04_PGRAPH_CTX_SWITCH3                            0x00400168
#define NV04_PGRAPH_CTX_SWITCH4                            0x0040016C
#define NV04_PGRAPH_CTX_CONTROL                            0x00400170
#define NV04_PGRAPH_CTX_USER                               0x00400174
#define NV04_PGRAPH_CTX_CACHE1                             0x00400180
#define NV03_PGRAPH_CTX_CONTROL                            0x00400190
#define NV03_PGRAPH_CTX_USER                               0x00400194
#define NV04_PGRAPH_CTX_CACHE2                             0x004001A0
#define NV04_PGRAPH_CTX_CACHE3                             0x004001C0
#define NV04_PGRAPH_CTX_CACHE4                             0x004001E0
#define NV40_PGRAPH_CTXCTL_0304                            0x00400304
#define NV40_PGRAPH_CTXCTL_0304_XFER_CTX                   0x00000001
#define NV40_PGRAPH_CTXCTL_UCODE_STAT                      0x00400308
#define NV40_PGRAPH_CTXCTL_UCODE_STAT_IP_MASK              0xff000000
#define NV40_PGRAPH_CTXCTL_UCODE_STAT_IP_SHIFT                     24
#define NV40_PGRAPH_CTXCTL_UCODE_STAT_OP_MASK              0x00ffffff
#define NV40_PGRAPH_CTXCTL_0310                            0x00400310
#define NV40_PGRAPH_CTXCTL_0310_XFER_SAVE                  0x00000020
#define NV40_PGRAPH_CTXCTL_0310_XFER_LOAD                  0x00000040
#define NV40_PGRAPH_CTXCTL_030C                            0x0040030c
#define NV40_PGRAPH_CTXCTL_UCODE_INDEX                     0x00400324
#define NV40_PGRAPH_CTXCTL_UCODE_DATA                      0x00400328
#define NV40_PGRAPH_CTXCTL_CUR                             0x0040032c
#define NV40_PGRAPH_CTXCTL_CUR_LOADED                      0x01000000
#define NV40_PGRAPH_CTXCTL_CUR_INSTANCE                    0x000FFFFF
#define NV40_PGRAPH_CTXCTL_NEXT                            0x00400330
#define NV40_PGRAPH_CTXCTL_NEXT_INSTANCE                   0x000fffff
#define NV50_PGRAPH_CTXCTL_CUR                             0x0040032c
#define NV50_PGRAPH_CTXCTL_CUR_LOADED                      0x80000000
#define NV50_PGRAPH_CTXCTL_CUR_INSTANCE                    0x00ffffff
#define NV50_PGRAPH_CTXCTL_NEXT                            0x00400330
#define NV50_PGRAPH_CTXCTL_NEXT_INSTANCE                   0x00ffffff
#define NV03_PGRAPH_ABS_X_RAM                              0x00400400
#define NV03_PGRAPH_ABS_Y_RAM                              0x00400480
#define NV03_PGRAPH_X_MISC                                 0x00400500
#define NV03_PGRAPH_Y_MISC                                 0x00400504
#define NV04_PGRAPH_VALID1                                 0x00400508
#define NV04_PGRAPH_SOURCE_COLOR                           0x0040050C
#define NV04_PGRAPH_MISC24_0                               0x00400510
#define NV03_PGRAPH_XY_LOGIC_MISC0                         0x00400514
#define NV03_PGRAPH_XY_LOGIC_MISC1                         0x00400518
#define NV03_PGRAPH_XY_LOGIC_MISC2                         0x0040051C
#define NV03_PGRAPH_XY_LOGIC_MISC3                         0x00400520
#define NV03_PGRAPH_CLIPX_0                                0x00400524
#define NV03_PGRAPH_CLIPX_1                                0x00400528
#define NV03_PGRAPH_CLIPY_0                                0x0040052C
#define NV03_PGRAPH_CLIPY_1                                0x00400530
#define NV03_PGRAPH_ABS_ICLIP_XMAX                         0x00400534
#define NV03_PGRAPH_ABS_ICLIP_YMAX                         0x00400538
#define NV03_PGRAPH_ABS_UCLIP_XMIN                         0x0040053C
#define NV03_PGRAPH_ABS_UCLIP_YMIN                         0x00400540
#define NV03_PGRAPH_ABS_UCLIP_XMAX                         0x00400544
#define NV03_PGRAPH_ABS_UCLIP_YMAX                         0x00400548
#define NV03_PGRAPH_ABS_UCLIPA_XMIN                        0x00400560
#define NV03_PGRAPH_ABS_UCLIPA_YMIN                        0x00400564
#define NV03_PGRAPH_ABS_UCLIPA_XMAX                        0x00400568
#define NV03_PGRAPH_ABS_UCLIPA_YMAX                        0x0040056C
#define NV04_PGRAPH_MISC24_1                               0x00400570
#define NV04_PGRAPH_MISC24_2                               0x00400574
#define NV04_PGRAPH_VALID2                                 0x00400578
#define NV04_PGRAPH_PASSTHRU_0                             0x0040057C
#define NV04_PGRAPH_PASSTHRU_1                             0x00400580
#define NV04_PGRAPH_PASSTHRU_2                             0x00400584
#define NV10_PGRAPH_DIMX_TEXTURE                           0x00400588
#define NV10_PGRAPH_WDIMX_TEXTURE                          0x0040058C
#define NV04_PGRAPH_COMBINE_0_ALPHA                        0x00400590
#define NV04_PGRAPH_COMBINE_0_COLOR                        0x00400594
#define NV04_PGRAPH_COMBINE_1_ALPHA                        0x00400598
#define NV04_PGRAPH_COMBINE_1_COLOR                        0x0040059C
#define NV04_PGRAPH_FORMAT_0                               0x004005A8
#define NV04_PGRAPH_FORMAT_1                               0x004005AC
#define NV04_PGRAPH_FILTER_0                               0x004005B0
#define NV04_PGRAPH_FILTER_1                               0x004005B4
#define NV03_PGRAPH_MONO_COLOR0                            0x00400600
#define NV04_PGRAPH_ROP3                                   0x00400604
#define NV04_PGRAPH_BETA_AND                               0x00400608
#define NV04_PGRAPH_BETA_PREMULT                           0x0040060C
#define NV04_PGRAPH_LIMIT_VIOL_PIX                         0x00400610
#define NV04_PGRAPH_FORMATS                                0x00400618
#define NV10_PGRAPH_DEBUG_2                                0x00400620
#define NV04_PGRAPH_BOFFSET0                               0x00400640
#define NV04_PGRAPH_BOFFSET1                               0x00400644
#define NV04_PGRAPH_BOFFSET2                               0x00400648
#define NV04_PGRAPH_BOFFSET3                               0x0040064C
#define NV04_PGRAPH_BOFFSET4                               0x00400650
#define NV04_PGRAPH_BOFFSET5                               0x00400654
#define NV04_PGRAPH_BBASE0                                 0x00400658
#define NV04_PGRAPH_BBASE1                                 0x0040065C
#define NV04_PGRAPH_BBASE2                                 0x00400660
#define NV04_PGRAPH_BBASE3                                 0x00400664
#define NV04_PGRAPH_BBASE4                                 0x00400668
#define NV04_PGRAPH_BBASE5                                 0x0040066C
#define NV04_PGRAPH_BPITCH0                                0x00400670
#define NV04_PGRAPH_BPITCH1                                0x00400674
#define NV04_PGRAPH_BPITCH2                                0x00400678
#define NV04_PGRAPH_BPITCH3                                0x0040067C
#define NV04_PGRAPH_BPITCH4                                0x00400680
#define NV04_PGRAPH_BLIMIT0                                0x00400684
#define NV04_PGRAPH_BLIMIT1                                0x00400688
#define NV04_PGRAPH_BLIMIT2                                0x0040068C
#define NV04_PGRAPH_BLIMIT3                                0x00400690
#define NV04_PGRAPH_BLIMIT4                                0x00400694
#define NV04_PGRAPH_BLIMIT5                                0x00400698
#define NV04_PGRAPH_BSWIZZLE2                              0x0040069C
#define NV04_PGRAPH_BSWIZZLE5                              0x004006A0
#define NV03_PGRAPH_STATUS                                 0x004006B0
#define NV04_PGRAPH_STATUS                                 0x00400700
#    define NV40_PGRAPH_STATUS_SYNC_STALL                  0x00004000
#define NV04_PGRAPH_TRAPPED_ADDR                           0x00400704
#define NV04_PGRAPH_TRAPPED_DATA                           0x00400708
#define NV04_PGRAPH_SURFACE                                0x0040070C
#define NV10_PGRAPH_TRAPPED_DATA_HIGH                      0x0040070C
#define NV04_PGRAPH_STATE                                  0x00400710
#define NV10_PGRAPH_SURFACE                                0x00400710
#define NV04_PGRAPH_NOTIFY                                 0x00400714
#define NV10_PGRAPH_STATE                                  0x00400714
#define NV10_PGRAPH_NOTIFY                                 0x00400718

#define NV04_PGRAPH_FIFO                                   0x00400720

#define NV04_PGRAPH_BPIXEL                                 0x00400724
#define NV10_PGRAPH_RDI_INDEX                              0x00400750
#define NV04_PGRAPH_FFINTFC_ST2                            0x00400754
#define NV10_PGRAPH_RDI_DATA                               0x00400754
#define NV04_PGRAPH_DMA_PITCH                              0x00400760
#define NV10_PGRAPH_FFINTFC_FIFO_PTR                       0x00400760
#define NV04_PGRAPH_DVD_COLORFMT                           0x00400764
#define NV10_PGRAPH_FFINTFC_ST2                            0x00400764
#define NV04_PGRAPH_SCALED_FORMAT                          0x00400768
#define NV10_PGRAPH_FFINTFC_ST2_DL                         0x00400768
#define NV10_PGRAPH_FFINTFC_ST2_DH                         0x0040076c
#define NV10_PGRAPH_DMA_PITCH                              0x00400770
#define NV10_PGRAPH_DVD_COLORFMT                           0x00400774
#define NV10_PGRAPH_SCALED_FORMAT                          0x00400778
#define NV20_PGRAPH_CHANNEL_CTX_TABLE                      0x00400780
#define NV20_PGRAPH_CHANNEL_CTX_POINTER                    0x00400784
#define NV20_PGRAPH_CHANNEL_CTX_XFER                       0x00400788
#define NV20_PGRAPH_CHANNEL_CTX_XFER_LOAD                  0x00000001
#define NV20_PGRAPH_CHANNEL_CTX_XFER_SAVE                  0x00000002
#define NV04_PGRAPH_PATT_COLOR0                            0x00400800
#define NV04_PGRAPH_PATT_COLOR1                            0x00400804
#define NV04_PGRAPH_PATTERN                                0x00400808
#define NV04_PGRAPH_PATTERN_SHAPE                          0x00400810
#define NV04_PGRAPH_CHROMA                                 0x00400814
#define NV04_PGRAPH_CONTROL0                               0x00400818
#define NV04_PGRAPH_CONTROL1                               0x0040081C
#define NV04_PGRAPH_CONTROL2                               0x00400820
#define NV04_PGRAPH_BLEND                                  0x00400824
#define NV04_PGRAPH_STORED_FMT                             0x00400830
#define NV04_PGRAPH_PATT_COLORRAM                          0x00400900
#define NV20_PGRAPH_TILE(i)                                (0x00400900 + (i*16))
#define NV20_PGRAPH_TLIMIT(i)                              (0x00400904 + (i*16))
#define NV20_PGRAPH_TSIZE(i)                               (0x00400908 + (i*16))
#define NV20_PGRAPH_TSTATUS(i)                             (0x0040090C + (i*16))
#define NV20_PGRAPH_ZCOMP(i)                               (0x00400980 + 4*(i))
#define NV10_PGRAPH_TILE(i)                                (0x00400B00 + (i*16))
#define NV10_PGRAPH_TLIMIT(i)                              (0x00400B04 + (i*16))
#define NV10_PGRAPH_TSIZE(i)                               (0x00400B08 + (i*16))
#define NV10_PGRAPH_TSTATUS(i)                             (0x00400B0C + (i*16))
#define NV04_PGRAPH_U_RAM                                  0x00400D00
#define NV47_PGRAPH_TILE(i)                                (0x00400D00 + (i*16))
#define NV47_PGRAPH_TLIMIT(i)                              (0x00400D04 + (i*16))
#define NV47_PGRAPH_TSIZE(i)                               (0x00400D08 + (i*16))
#define NV47_PGRAPH_TSTATUS(i)                             (0x00400D0C + (i*16))
#define NV04_PGRAPH_V_RAM                                  0x00400D40
#define NV04_PGRAPH_W_RAM                                  0x00400D80
#define NV10_PGRAPH_COMBINER0_IN_ALPHA                     0x00400E40
#define NV10_PGRAPH_COMBINER1_IN_ALPHA                     0x00400E44
#define NV10_PGRAPH_COMBINER0_IN_RGB                       0x00400E48
#define NV10_PGRAPH_COMBINER1_IN_RGB                       0x00400E4C
#define NV10_PGRAPH_COMBINER_COLOR0                        0x00400E50
#define NV10_PGRAPH_COMBINER_COLOR1                        0x00400E54
#define NV10_PGRAPH_COMBINER0_OUT_ALPHA                    0x00400E58
#define NV10_PGRAPH_COMBINER1_OUT_ALPHA                    0x00400E5C
#define NV10_PGRAPH_COMBINER0_OUT_RGB                      0x00400E60
#define NV10_PGRAPH_COMBINER1_OUT_RGB                      0x00400E64
#define NV10_PGRAPH_COMBINER_FINAL0                        0x00400E68
#define NV10_PGRAPH_COMBINER_FINAL1                        0x00400E6C
#define NV10_PGRAPH_WINDOWCLIP_HORIZONTAL                  0x00400F00
#define NV10_PGRAPH_WINDOWCLIP_VERTICAL                    0x00400F20
#define NV10_PGRAPH_XFMODE0                                0x00400F40
#define NV10_PGRAPH_XFMODE1                                0x00400F44
#define NV10_PGRAPH_GLOBALSTATE0                           0x00400F48
#define NV10_PGRAPH_GLOBALSTATE1                           0x00400F4C
#define NV10_PGRAPH_PIPE_ADDRESS                           0x00400F50
#define NV10_PGRAPH_PIPE_DATA                              0x00400F54
#define NV04_PGRAPH_DMA_START_0                            0x00401000
#define NV04_PGRAPH_DMA_START_1                            0x00401004
#define NV04_PGRAPH_DMA_LENGTH                             0x00401008
#define NV04_PGRAPH_DMA_MISC                               0x0040100C
#define NV04_PGRAPH_DMA_DATA_0                             0x00401020
#define NV04_PGRAPH_DMA_DATA_1                             0x00401024
#define NV04_PGRAPH_DMA_RM                                 0x00401030
#define NV04_PGRAPH_DMA_A_XLATE_INST                       0x00401040
#define NV04_PGRAPH_DMA_A_CONTROL                          0x00401044
#define NV04_PGRAPH_DMA_A_LIMIT                            0x00401048
#define NV04_PGRAPH_DMA_A_TLB_PTE                          0x0040104C
#define NV04_PGRAPH_DMA_A_TLB_TAG                          0x00401050
#define NV04_PGRAPH_DMA_A_ADJ_OFFSET                       0x00401054
#define NV04_PGRAPH_DMA_A_OFFSET                           0x00401058
#define NV04_PGRAPH_DMA_A_SIZE                             0x0040105C
#define NV04_PGRAPH_DMA_A_Y_SIZE                           0x00401060
#define NV04_PGRAPH_DMA_B_XLATE_INST                       0x00401080
#define NV04_PGRAPH_DMA_B_CONTROL                          0x00401084
#define NV04_PGRAPH_DMA_B_LIMIT                            0x00401088
#define NV04_PGRAPH_DMA_B_TLB_PTE                          0x0040108C
#define NV04_PGRAPH_DMA_B_TLB_TAG                          0x00401090
#define NV04_PGRAPH_DMA_B_ADJ_OFFSET                       0x00401094
#define NV04_PGRAPH_DMA_B_OFFSET                           0x00401098
#define NV04_PGRAPH_DMA_B_SIZE                             0x0040109C
#define NV04_PGRAPH_DMA_B_Y_SIZE                           0x004010A0
#define NV40_PGRAPH_TILE1(i)                               (0x00406900 + (i*16))
#define NV40_PGRAPH_TLIMIT1(i)                             (0x00406904 + (i*16))
#define NV40_PGRAPH_TSIZE1(i)                              (0x00406908 + (i*16))
#define NV40_PGRAPH_TSTATUS1(i)                            (0x0040690C + (i*16))

#endif
