Tuner drivers
=============

Simple tuner Programming
------------------------

There are some flavors of Tuner programming APIs.
These differ mainly by the bandswitch byte.

- L= LG_API       (VHF_LO=0x01, VHF_HI=0x02, UHF=0x08, radio=0x04)
- P= PHILIPS_API  (VHF_LO=0xA0, VHF_HI=0x90, UHF=0x30, radio=0x04)
- T= TEMIC_API    (VHF_LO=0x02, VHF_HI=0x04, UHF=0x01)
- A= ALPS_API     (VHF_LO=0x14, VHF_HI=0x12, UHF=0x11)
- M= PHILIPS_MK3  (VHF_LO=0x01, VHF_HI=0x02, UHF=0x04, radio=0x19)

Tuner Manufacturers
-------------------

- SAMSUNG Tuner identification: (e.g. TCPM9091PD27)

.. code-block:: none

 TCP [ABCJLMNQ] 90[89][125] [DP] [ACD] 27 [ABCD]
 [ABCJLMNQ]:
   A= BG+DK
   B= BG
   C= I+DK
   J= NTSC-Japan
   L= Secam LL
   M= BG+I+DK
   N= NTSC
   Q= BG+I+DK+LL
 [89]: ?
 [125]:
   2: No FM
   5: With FM
 [DP]:
   D= NTSC
   P= PAL
 [ACD]:
   A= F-connector
   C= Phono connector
   D= Din Jack
 [ABCD]:
   3-wire/I2C tuning, 2-band/3-band

These Tuners are PHILIPS_API compatible.

Philips Tuner identification: (e.g. FM1216MF)

.. code-block:: none

  F[IRMQ]12[1345]6{MF|ME|MP}
  F[IRMQ]:
   FI12x6: Tuner Series
   FR12x6: Tuner + Radio IF
   FM12x6: Tuner + FM
   FQ12x6: special
   FMR12x6: special
   TD15xx: Digital Tuner ATSC
  12[1345]6:
   1216: PAL BG
   1236: NTSC
   1246: PAL I
   1256: Pal DK
  {MF|ME|MP}
   MF: BG LL w/ Secam (Multi France)
   ME: BG DK I LL   (Multi Europe)
   MP: BG DK I      (Multi PAL)
   MR: BG DK M (?)
   MG: BG DKI M (?)
  MK2 series PHILIPS_API, most tuners are compatible to this one !
  MK3 series introduced in 2002 w/ PHILIPS_MK3_API

Temic Tuner identification: (.e.g 4006FH5)

.. code-block:: none

   4[01][0136][269]F[HYNR]5
    40x2: Tuner (5V/33V), TEMIC_API.
    40x6: Tuner 5V
    41xx: Tuner compact
    40x9: Tuner+FM compact
   [0136]
    xx0x: PAL BG
    xx1x: Pal DK, Secam LL
    xx3x: NTSC
    xx6x: PAL I
   F[HYNR]5
    FH5: Pal BG
    FY5: others
    FN5: multistandard
    FR5: w/ FM radio
   3X xxxx: order number with specific connector
  Note: Only 40x2 series has TEMIC_API, all newer tuners have PHILIPS_API.

LG Innotek Tuner:

- TPI8NSR11 : NTSC J/M    (TPI8NSR01 w/FM)  (P,210/497)
- TPI8PSB11 : PAL B/G     (TPI8PSB01 w/FM)  (P,170/450)
- TAPC-I701 : PAL I       (TAPC-I001 w/FM)  (P,170/450)
- TPI8PSB12 : PAL D/K+B/G (TPI8PSB02 w/FM)  (P,170/450)
- TAPC-H701P: NTSC_JP     (TAPC-H001P w/FM) (L,170/450)
- TAPC-G701P: PAL B/G     (TAPC-G001P w/FM) (L,170/450)
- TAPC-W701P: PAL I       (TAPC-W001P w/FM) (L,170/450)
- TAPC-Q703P: PAL D/K     (TAPC-Q001P w/FM) (L,170/450)
- TAPC-Q704P: PAL D/K+I   (L,170/450)
- TAPC-G702P: PAL D/K+B/G (L,170/450)

- TADC-H002F: NTSC (L,175/410?; 2-B, C-W+11, W+12-69)
- TADC-M201D: PAL D/K+B/G+I (L,143/425)  (sound control at I2C address 0xc8)
- TADC-T003F: NTSC Taiwan  (L,175/410?; 2-B, C-W+11, W+12-69)

Suffix:
  - P= Standard phono female socket
  - D= IEC female socket
  - F= F-connector

Other Tuners:

- TCL2002MB-1 : PAL BG + DK       =TUNER_LG_PAL_NEW_TAPC
- TCL2002MB-1F: PAL BG + DK w/FM  =PHILIPS_PAL
- TCL2002MI-2 : PAL I		= ??

ALPS Tuners:

- Most are LG_API compatible
- TSCH6 has ALPS_API (TSCH5 ?)
- TSBE1 has extra API 05,02,08 Control_byte=0xCB Source:[#f1]_

.. [#f1] conexant100029b-PCI-Decoder-ApplicationNote.pdf
