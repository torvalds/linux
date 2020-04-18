.. SPDX-License-Identifier: GPL-2.0

Frontend drivers
================

.. note::

  #) There is no guarantee that every frontend driver works
     out of the box with every card, because of different wiring.

  #) The demodulator chips can be used with a variety of
     tuner/PLL chips, and not all combinations are supported. Often
     the demodulator and tuner/PLL chip are inside a metal box for
     shielding, and the whole metal box has its own part number.

  - dvb_dummy_fe: for testing...

  DVB-S:
   - ves1x93		: Alps BSRV2 (ves1893 demodulator) and dbox2 (ves1993)
   - cx24110		: Conexant HM1221/HM1811 (cx24110 or cx24106 demod, cx24108 PLL)
   - grundig_29504-491	: Grundig 29504-491 (Philips TDA8083 demodulator), tsa5522 PLL
   - mt312		: Zarlink mt312 or Mitel vp310 demodulator, sl1935 or tsa5059 PLLi, Technisat Sky2Pc with bios Rev. 2.3
   - stv0299		: Alps BSRU6 (tsa5059 PLL), LG TDQB-S00x (tsa5059 PLL),
			  LG TDQF-S001F (sl1935 PLL), Philips SU1278 (tua6100 PLL),
			  Philips SU1278SH (tsa5059 PLL), Samsung TBMU24112IMB, Technisat Sky2Pc with bios Rev. 2.6

  DVB-C:
   - ves1820		: various (ves1820 demodulator, sp5659c or spXXXX PLL)
   - at76c651		: Atmel AT76c651(B) with DAT7021 PLL

  DVB-T:
   - alps_tdlb7		: Alps TDLB7 (sp8870 demodulator, sp5659 PLL)
   - alps_tdmb7		: Alps TDMB7 (cx22700 demodulator)
   - grundig_29504-401	: Grundig 29504-401 (LSI L64781 demodulator), tsa5060 PLL
   - tda1004x		: Philips tda10045h (td1344 or tdm1316l PLL)
   - nxt6000 		: Alps TDME7 (MITEL SP5659 PLL), Alps TDED4 (TI ALP510 PLL), Comtech DVBT-6k07 (SP5730 PLL), (NxtWave Communications NXT6000 demodulator)
   - sp887x		: Microtune 7202D
   - dib3000mb	: DiBcom 3000-MB demodulator

  DVB-S/C/T:
   - dst		: TwinHan DST Frontend

  ATSC:
   - nxt200x		: Nxtwave NXT2002 & NXT2004
   - or51211		: or51211 based (pcHDTV HD2000 card)
   - or51132		: or51132 based (pcHDTV HD3000 card)
   - bcm3510		: Broadcom BCM3510
   - lgdt330x		: LG Electronics DT3302 & DT3303
