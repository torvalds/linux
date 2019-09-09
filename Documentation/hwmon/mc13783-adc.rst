Kernel driver mc13783-adc
=========================

Supported chips:

  * Freescale MC13783

    Prefix: 'mc13783'

    Datasheet: https://www.nxp.com/docs/en/data-sheet/MC13783.pdf

  * Freescale MC13892

    Prefix: 'mc13892'

    Datasheet: https://www.nxp.com/docs/en/data-sheet/MC13892.pdf



Authors:

   - Sascha Hauer <s.hauer@pengutronix.de>
   - Luotao Fu <l.fu@pengutronix.de>

Description
-----------

The Freescale MC13783 and MC13892 are Power Management and Audio Circuits.
Among other things they contain a 10-bit A/D converter. The converter has 16
(MC13783) resp. 12 (MC13892) channels which can be used in different modes. The
A/D converter has a resolution of 2.25mV.

Some channels can be used as General Purpose inputs or in a dedicated mode with
a chip internal scaling applied .

Currently the driver only supports the Application Supply channel (BP / BPSNS),
the General Purpose inputs and touchscreen.

See the following tables for the meaning of the different channels and their
chip internal scaling:

- MC13783:

======= =============================================== =============== =======
Channel	Signal						Input Range	Scaling
======= =============================================== =============== =======
0	Battery Voltage (BATT)				2.50 - 4.65V	-2.40V
1	Battery Current (BATT - BATTISNS)		-50 - 50 mV	x20
2	Application Supply (BP)				2.50 - 4.65V	-2.40V
3	Charger Voltage (CHRGRAW)			0 - 10V /	/5
							0 - 20V		/10
4	Charger Current (CHRGISNSP-CHRGISNSN)		-0.25 - 0.25V	x4
5	General Purpose ADIN5 / Battery Pack Thermistor	0 - 2.30V	No
6	General Purpose ADIN6 / Backup Voltage (LICELL)	0 - 2.30V /	No /
							1.50 - 3.50V	-1.20V
7	General Purpose ADIN7 / UID / Die Temperature	0 - 2.30V /	No /
							0 - 2.55V /	x0.9 / No
8	General Purpose ADIN8				0 - 2.30V	No
9	General Purpose ADIN9				0 - 2.30V	No
10	General Purpose ADIN10				0 - 2.30V	No
11	General Purpose ADIN11				0 - 2.30V	No
12	General Purpose TSX1 / Touchscreen X-plate 1	0 - 2.30V	No
13	General Purpose TSX2 / Touchscreen X-plate 2	0 - 2.30V	No
14	General Purpose TSY1 / Touchscreen Y-plate 1	0 - 2.30V	No
15	General Purpose TSY2 / Touchscreen Y-plate 2	0 - 2.30V	No
======= =============================================== =============== =======

- MC13892:

======= =============================================== =============== =======
Channel	Signal						Input Range	Scaling
======= =============================================== =============== =======
0	Battery Voltage (BATT)				0 - 4.8V	/2
1	Battery Current (BATT - BATTISNSCC)		-60 - 60 mV	x20
2	Application Supply (BPSNS)			0 - 4.8V	/2
3	Charger Voltage (CHRGRAW)			0 - 12V /	/5
							0 - 20V		/10
4	Charger Current (CHRGISNS-BPSNS) /		-0.3 - 0.3V /	x4 /
	Touchscreen X-plate 1				0 - 2.4V	No
5	General Purpose ADIN5 /	Battery Pack Thermistor	0 - 2.4V	No
6	General Purpose ADIN6 / Backup Voltage (LICELL)	0 - 2.4V /	No
	Backup Voltage (LICELL)                        	0 - 3.6V	x2/3
7	General Purpose ADIN7 / UID / Die Temperature	0 - 2.4V /	No /
							0 - 4.8V	/2
12	General Purpose TSX1 / Touchscreen X-plate 1	0 - 2.4V	No
13	General Purpose TSX2 / Touchscreen X-plate 2	0 - 2.4V	No
14	General Purpose TSY1 / Touchscreen Y-plate 1	0 - 2.4V	No
15	General Purpose TSY2 / Touchscreen Y-plate 2	0 - 2.4V	No
======= =============================================== =============== =======
