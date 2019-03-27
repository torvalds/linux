/****************************************************************************/
/*      gps.h                                                            */     
/*      TrueTime GPS-VME and VME-SG                                     */
/*      VME controller hardware commands and parameters.                */
/*      created 010694 res                                              */
/*      History:        revised for 747i 3/94                           */
/****************************************************************************/


#define GPS_VME "/dev/vme2"    /* the device file for the GPS board */
                               /* change it to whatever yours is */
#define PRIO    120               /* set the realtime priority */
#define NREGS 7                   /* number of registers we will use */

#define GFRZ1   0x0020          /* freeze cmd addr gen reg. 1 */
#define GREG1A  0x0021  /* Gen reg. 1 Word A (units microsec to 0.001 sec) */
#define GREG1B  0x0040  /* Gen reg. 1 Word B (units 0.01 sec to tens sec ) */
#define GREG1C  0x0041  /* Gen reg  1 Word C (units mins and hours) */
#define GREG1D  0x0042  /* Gen reg. 1 Word D (units days and status) */
#define GREG1E  0x0043  /* Gen reg. 1 Word E (units Years ) */
#define GUFRZ1  0x0022  /* unfreeze cmd addr gen reg 1 */

#define MASKDAY 0x0FFF  /* mask for units days */
#define MASKHI  0xFF00
#define MASKLO  0x00FF
/* Use the following ASCII hex values: N(0x004e),S(0x0053),E(0x0045),
        W(0x0057), +(0x002B), - (0x002D)   */

#define LAT1    0x0048  /* Lat (degrees) */
#define LAT2    0x0049  /* Lat (min, sec) */
#define LAT3    0x004A  /* Lat (N/S, tenths sec) */
#define LON1    0x004B  /* Lon (degrees) */
#define LON2    0x004C  /* Lon (min, sec) */
#define LON3    0x004D  /* Lon (E/W, tenths sec) */
#define ELV1    0x004E  /* Elev. (sign, 10,000 and 1000 ) */
#define ELV2    0x004F  /* Elev. (100, 10s, units, and .1) */

#define CFREG1  0x0050  /* config. register 1 */
#define CFREG2  0x00A0  /* config. register 2 */
#define PMODE   0x00A4  /* Position mode */
#define LOCAL   0x0051  /* Local hours offset */
#define RATE    0x0054  /* Pulse rate output select */
#define DAC     0x0055  /* OSC Control (DAC) select */

#define PUMS    0x0056  /* Gen. preset register unit millisec */
#define PMS     0x0057  /* Gen. preset register units hundreds and tens ms */
#define PSEC    0x0058  /* Gen. preset register units tens and unit seconds */
#define PMIN    0x0059  /* Gen. preset register units tens and unit minutes */
#define PHRS    0x005A  /* Gen. preset register units tens and unit hours */
#define PDYS1   0x005B  /* Gen. preset register units tens and unit days */
#define PDYS2   0x005C  /* Gen. preset register units hundreds days */
#define PYRS1   0x005D  /* Gen. preset register units tens and unit years */
#define PYRS2   0x005E  /* Gen. preset reg. units thousands and hundreds yrs */
