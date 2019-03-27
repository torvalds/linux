/*
 * /src/NTP/ntp4-dev/include/trimble.h,v 4.6 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * trimble.h,v 4.6 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * $Created: Sun Aug  2 16:16:49 1998 $
 *
 * Copyright (c) 1998-2005 by Frank Kardel <kardel <AT> ntp.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#ifndef TRIMBLE_H
#define TRIMBLE_H

/*
 * Trimble packet command codes - commands being sent/received
 * keep comments formatted as shown - they are used to generate
 * translation tables
 */
#define CMD_CCLROSC	0x1D	/* clear oscillator offset */
#define CMD_CCLRRST	0x1E	/* clear battery backup and RESET */
#define CMD_CVERSION	0x1F	/* return software version */
#define CMD_CALMANAC	0x20	/* almanac */
#define CMD_CCURTIME	0x21	/* current time */
#define CMD_CMODESEL	0x22	/* mode select (2-d, 3-D, auto) */
#define CMD_CINITPOS	0x23	/* initial position */
#define	CMD_CRECVPOS	0x24	/* receiver position fix mode */
#define CMD_CRESET	0x25	/* soft reset & selftest */
#define CMD_CRECVHEALTH	0x26	/* receiver health */
#define CMD_CSIGNALLV	0x27	/* signal levels */
#define CMD_CMESSAGE	0x28	/* GPS system message */
#define CMD_CALMAHEALTH	0x29	/* almanac healt page */
#define CMD_C2DALTITUDE	0x2A	/* altitude for 2-D mode */
#define CMD_CINITPOSLLA	0x2B	/* initial position LLA */
#define CMD_COPERPARAM	0x2C	/* operating parameters */
#define CMD_COSCOFFSET	0x2D	/* oscillator offset */
#define CMD_CSETGPSTIME	0x2E	/* set GPS time */
#define CMD_CUTCPARAM	0x2F	/* UTC parameters */
#define CMD_CACCPOSXYZ	0x31	/* accurate initial position (XYZ/ECEF) */
#define CMD_CACCPOS	0x32	/* accurate initial position */
#define CMD_CANALOGDIG	0x33	/* analog to digital */
#define CMD_CSAT1SAT	0x34	/* satellite for 1-Sat mode */
#define CMD_CIOOPTIONS	0x35	/* I/O options */
#define CMD_CVELOCAID	0x36	/* velocity aiding of acquisition */
#define CMD_CSTATLSTPOS	0x37	/* status and values of last pos. and vel. */
#define CMD_CLOADSSATDT	0x38	/* load satellite system data */
#define CMD_CSATDISABLE	0x39	/* satellite disable */
#define CMD_CLASTRAW	0x3A	/* last raw measurement */
#define CMD_CSTATSATEPH	0x3B	/* satellite ephemeris status */
#define CMD_CSTATTRACK	0x3C	/* tracking status */
#define CMD_CCHANADGPS	0x3D	/* configure channel A for differential GPS */
#define CMD_CADDITFIX	0x3E	/* additional fix data */
#define CMD_CDGPSFIXMD	0x62	/* set/request differential GPS position fix mode */
#define CMD_CDGPSCORR	0x65	/* differential correction status */
#define CMD_CPOSFILT	0x71	/* position filter parameters */
#define CMD_CHEIGHTFILT	0x73	/* height filter control */
#define CMD_CHIGH8CNT	0x75	/* high-8 (best 4) / high-6 (overdetermined) control */
#define CMD_CMAXDGPSCOR	0x77	/* maximum rate of DGPS corrections */
#define CMD_CSUPER	0x8E	/* super paket */

#define CMD_RDATAA	0x3D	/* data channel A configuration:trimble_channelA:RO */
#define CMD_RALMANAC	0x40	/* almanac data for sat:gps_almanac:RO */
#define CMD_RCURTIME	0x41	/* GPS time:gps_time:RO */
#define CMD_RSPOSXYZ	0x42	/* single precision XYZ position:gps_position(XYZ):RO|DEF */
#define CMD_RVELOXYZ	0x43	/* velocity fix (XYZ ECEF):gps_velocity(XYZ):RO|DEF */
#define	CMD_RBEST4	0x44	/* best 4 satellite selection:trimble_best4:RO|DEF */
#define CMD_RVERSION	0x45	/* software version:trimble_version:RO|DEF */
#define CMD_RRECVHEALTH	0x46	/* receiver health:trimble_receiver_health:RO|DEF */
#define CMD_RSIGNALLV	0x47	/* signal levels of all satellites:trimble_signal_levels:RO */
#define CMD_RMESSAGE	0x48	/* GPS system message:gps-message:RO|DEF */
#define CMD_RALMAHEALTH	0x49	/* almanac health page for all satellites:gps_almanac_health:RO */
#define CMD_RSLLAPOS	0x4A	/* single LLA position:gps_position(LLA):RO|DEF */
#define CMD_RMACHSTAT	0x4B	/* machine code / status:trimble_status:RO|DEF */
#define CMD_ROPERPARAM	0x4C	/* operating parameters:trimble_opparam:RO */
#define CMD_ROSCOFFSET	0x4D	/* oscillator offset:trimble_oscoffset:RO */
#define CMD_RSETGPSTIME	0x4E	/* response to set GPS time:trimble_setgpstime:RO */
#define CMD_RUTCPARAM	0x4F	/* UTC parameters:gps_utc_correction:RO|DEF */
#define CMD_RANALOGDIG	0x53	/* analog to digital:trimble_analogdigital:RO */
#define CMD_RSAT1BIAS	0x54	/* one-satellite bias & bias rate:trimble_sat1bias:RO */
#define CMD_RIOOPTIONS	0x55	/* I/O options:trimble_iooptions:RO */
#define CMD_RVELOCFIX	0x56	/* velocity fix (ENU):trimble_velocfix */
#define CMD_RSTATLSTFIX	0x57	/* status and values of last pos. and vel.:trimble_status_lastpos:RO */
#define CMD_RLOADSSATDT	0x58	/* response to load satellite system data:trimble_loaddata:RO */
#define CMD_RSATDISABLE	0x59	/* satellite disable:trimble_satdisble:RO */
#define CMD_RLASTRAW	0x5A	/* last raw measurement:trimble_lastraw:RO */
#define CMD_RSTATSATEPH	0x5B	/* satellite ephemeris status:trimble_ephstatus:RO */
#define CMD_RSTATTRACK	0x5C	/* tracking status:trimble_tracking_status:RO|DEF */
#define CMD_RADDITFIX	0x5E	/* additional fix data:trimble_addfix:RO */
#define CMD_RALLINVIEW	0x6D	/* all in view satellite selection:trimble_satview:RO|DEF */
#define CMD_RPOSFILT	0x72	/* position filter parameters:trimble_posfilt:RO */
#define CMD_RHEIGHTFILT	0x74	/* height filter control:trimble_heightfilt:RO */
#define CMD_RHIGH8CNT	0x76	/* high-8 (best 4) / high-6 (overdetermined) control:trimble_high8control:RO */
#define CMD_RMAXAGE	0x78	/* DC MaxAge:trimble_dgpsmaxage:RO */
#define CMD_RDGPSFIX	0x82	/* differential position fix mode:trimble_dgpsfixmode:RO */
#define CMD_RDOUBLEXYZ	0x83	/* double precision XYZ:gps_position_ext(XYZ):RO|DEF */
#define CMD_RDOUBLELLA	0x84	/* double precision LLA:gps_position_ext(LLA):RO|DEF */
#define CMD_RDGPSSTAT	0x85	/* differential correction status:trimble_dgpsstatus:RO */
#define CMD_RSUPER	0x8F	/* super paket::0 */

typedef struct cmd_info
{
  unsigned char cmd;		/* command code */
  const char   *cmdname;	/* command name */
  const char   *cmddesc;	/* command description */
  const char   *varname;	/* name of variable */
  int           varmode;	/* mode of variable */
} cmd_info_t;

extern cmd_info_t trimble_rcmds[];
extern cmd_info_t trimble_scmds[];

extern cmd_info_t *trimble_convert (unsigned int cmd, cmd_info_t *tbl);

#endif
/*
 * History:
 *
 * trimble.h,v
 * Revision 4.6  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.5  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.4  1999/02/28 11:41:11  kardel
 * (CMD_RUTCPARAM): control variable name unification
 *
 * Revision 4.3  1998/12/20 23:45:25  kardel
 * fix types and warnings
 *
 * Revision 4.2  1998/08/16 18:45:05  kardel
 * (CMD_RSTATTRACK): renamed mode 6 variable name
 *
 * Revision 4.1  1998/08/09 22:24:35  kardel
 * Trimble TSIP support
 *
 */
