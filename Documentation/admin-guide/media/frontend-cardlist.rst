.. SPDX-License-Identifier: GPL-2.0

================
Frontend drivers
================

.. note::

  #) There is no guarantee that every frontend driver works
     out of the box with every card, because of different wiring.

  #) The demodulator chips can be used with a variety of
     tuner/PLL chips, and not all combinations are supported. Often
     the demodulator and tuner/PLL chip are inside a metal box for
     shielding, and the whole metal box has its own part number.


Common Interface (EN50221) controller drivers
=============================================

==============  =========================================================
Driver          Name
==============  =========================================================
cxd2099         Sony CXD2099AR Common Interface driver
sp2             CIMaX SP2
==============  =========================================================

ATSC (North American/Korean Terrestrial/Cable DTV) frontends
============================================================

==============  =========================================================
Driver          Name
==============  =========================================================
au8522_dig      Auvitek AU8522 based DTV demod
au8522_decoder  Auvitek AU8522 based ATV demod
bcm3510         Broadcom BCM3510
lg2160          LG Electronics LG216x based
lgdt3305        LG Electronics LGDT3304 and LGDT3305 based
lgdt3306a       LG Electronics LGDT3306A based
lgdt330x        LG Electronics LGDT3302/LGDT3303 based
nxt200x         NxtWave Communications NXT2002/NXT2004 based
or51132         Oren OR51132 based
or51211         Oren OR51211 based
s5h1409         Samsung S5H1409 based
s5h1411         Samsung S5H1411 based
==============  =========================================================

DVB-C (cable) frontends
=======================

==============  =========================================================
Driver          Name
==============  =========================================================
stv0297         ST STV0297 based
tda10021        Philips TDA10021 based
tda10023        Philips TDA10023 based
ves1820         VLSI VES1820 based
==============  =========================================================

DVB-S (satellite) frontends
===========================

==============  =========================================================
Driver          Name
==============  =========================================================
cx24110         Conexant CX24110 based
cx24116         Conexant CX24116 based
cx24117         Conexant CX24117 based
cx24120         Conexant CX24120 based
cx24123         Conexant CX24123 based
ds3000          Montage Technology DS3000 based
mb86a16         Fujitsu MB86A16 based
mt312           Zarlink VP310/MT312/ZL10313 based
s5h1420         Samsung S5H1420 based
si21xx          Silicon Labs SI21XX based
stb6000         ST STB6000 silicon tuner
stv0288         ST STV0288 based
stv0299         ST STV0299 based
stv0900         ST STV0900 based
stv6110         ST STV6110 silicon tuner
tda10071        NXP TDA10071
tda10086        Philips TDA10086 based
tda8083         Philips TDA8083 based
tda8261         Philips TDA8261 based
tda826x         Philips TDA826X silicon tuner
ts2020          Montage Technology TS2020 based tuners
tua6100         Infineon TUA6100 PLL
cx24113         Conexant CX24113/CX24128 tuner for DVB-S/DSS
itd1000         Integrant ITD1000 Zero IF tuner for DVB-S/DSS
ves1x93         VLSI VES1893 or VES1993 based
zl10036         Zarlink ZL10036 silicon tuner
zl10039         Zarlink ZL10039 silicon tuner
==============  =========================================================

DVB-T (terrestrial) frontends
=============================

==============  =========================================================
Driver          Name
==============  =========================================================
af9013          Afatech AF9013 demodulator
cx22700         Conexant CX22700 based
cx22702         Conexant cx22702 demodulator (OFDM)
cxd2820r        Sony CXD2820R
cxd2841er       Sony CXD2841ER
cxd2880         Sony CXD2880 DVB-T2/T tuner + demodulator
dib3000mb       DiBcom 3000M-B
dib3000mc       DiBcom 3000P/M-C
dib7000m        DiBcom 7000MA/MB/PA/PB/MC
dib7000p        DiBcom 7000PC
dib9000         DiBcom 9000
drxd            Micronas DRXD driver
ec100           E3C EC100
l64781          LSI L64781
mt352           Zarlink MT352 based
nxt6000         NxtWave Communications NXT6000 based
rtl2830         Realtek RTL2830 DVB-T
rtl2832         Realtek RTL2832 DVB-T
rtl2832_sdr     Realtek RTL2832 SDR
s5h1432         Samsung s5h1432 demodulator (OFDM)
si2168          Silicon Labs Si2168
sp8870          Spase sp8870 based
sp887x          Spase sp887x based
stv0367         ST STV0367 based
tda10048        Philips TDA10048HN based
tda1004x        Philips TDA10045H/TDA10046H based
zd1301_demod    ZyDAS ZD1301
zl10353         Zarlink ZL10353 based
==============  =========================================================

Digital terrestrial only tuners/PLL
===================================

==============  =========================================================
Driver          Name
==============  =========================================================
dvb-pll         Generic I2C PLL based tuners
dib0070         DiBcom DiB0070 silicon base-band tuner
dib0090         DiBcom DiB0090 silicon base-band tuner
==============  =========================================================

ISDB-S (satellite) & ISDB-T (terrestrial) frontends
===================================================

==============  =========================================================
Driver          Name
==============  =========================================================
mn88443x        Socionext MN88443x
tc90522         Toshiba TC90522
==============  =========================================================

ISDB-T (terrestrial) frontends
==============================

==============  =========================================================
Driver          Name
==============  =========================================================
dib8000         DiBcom 8000MB/MC
mb86a20s        Fujitsu mb86a20s
s921            Sharp S921 frontend
==============  =========================================================

Multistandard (cable + terrestrial) frontends
=============================================

==============  =========================================================
Driver          Name
==============  =========================================================
drxk            Micronas DRXK based
mn88472         Panasonic MN88472
mn88473         Panasonic MN88473
si2165          Silicon Labs si2165 based
tda18271c2dd    NXP TDA18271C2 silicon tuner
==============  =========================================================

Multistandard (satellite) frontends
===================================

==============  =========================================================
Driver          Name
==============  =========================================================
m88ds3103       Montage Technology M88DS3103
mxl5xx          MaxLinear MxL5xx based tuner-demodulators
stb0899         STB0899 based
stb6100         STB6100 based tuners
stv090x         STV0900/STV0903(A/B) based
stv0910         STV0910 based
stv6110x        STV6110/(A) based tuners
stv6111         STV6111 based tuners
==============  =========================================================

SEC control devices for DVB-S
=============================

==============  =========================================================
Driver          Name
==============  =========================================================
a8293           Allegro A8293
af9033          Afatech AF9033 DVB-T demodulator
ascot2e         Sony Ascot2E tuner
atbm8830        AltoBeam ATBM8830/8831 DMB-TH demodulator
drx39xyj        Micronas DRX-J demodulator
helene          Sony HELENE Sat/Ter tuner (CXD2858ER)
horus3a         Sony Horus3A tuner
isl6405         ISL6405 SEC controller
isl6421         ISL6421 SEC controller
isl6423         ISL6423 SEC controller
ix2505v         Sharp IX2505V silicon tuner
lgs8gl5         Silicon Legend LGS-8GL5 demodulator (OFDM)
lgs8gxx         Legend Silicon LGS8913/LGS8GL5/LGS8GXX DMB-TH demodulator
lnbh25          LNBH25 SEC controller
lnbh29          LNBH29 SEC controller
lnbp21          LNBP21/LNBH24 SEC controllers
lnbp22          LNBP22 SEC controllers
m88rs2000       M88RS2000 DVB-S demodulator and tuner
tda665x         TDA665x tuner
==============  =========================================================

Tools to develop new frontends
==============================

==============  =========================================================
Driver          Name
==============  =========================================================
dvb_dummy_fe    Dummy frontend driver
==============  =========================================================
