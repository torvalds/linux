/*
 * /src/NTP/REPOSITORY/ntp4-dev/include/mbg_gps166.h,v 4.7 2006/06/22 18:41:43 kardel RELEASE_20060622_A
 *
 * mbg_gps166.h,v 4.7 2006/06/22 18:41:43 kardel RELEASE_20060622_A
 *
 * $Created: Sun Jul 20 09:20:50 1997 $
 *
 * File GPSSERIO.H Copyright (c) by Meinberg Funkuhren (www.meinberg.de)
 *
 * Linkage to PARSE:
 * Copyright (c) 1997-2005 by Frank Kardel <kardel <AT> ntp.org>
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
#ifndef MBG_GPS166_H
#define MBG_GPS166_H


/***************************************************************************
 *
 *  Definitions taken from Meinberg's gpsserio.h and gpsdefs.h files.
 *
 *  Author:  Martin Burnicki, Meinberg Funkuhren
 *
 *  Copyright (c) Meinberg Funkuhren, Bad Pyrmont, Germany
 *
 *  Description:
 *    Structures and codes to be used to access Meinberg GPS clocks via
 *    their serial interface COM0. COM0 should be set to a high baud rate,
 *    default is 19200.
 *
 *    Standard Meinberg GPS serial operation is to send the Meinberg
 *    standard time string automatically once per second, once per
 *    minute, or on request per ASCII '?'.
 *
 *    GPS parameter setup or parameter readout uses blocks of binary
 *    data which have to be isolated from the standard string. A block
 *    of data starts with a SOH code (ASCII Start Of Header, 0x01)
 *    followed by a message header with constant length and a block of
 *    data with variable length.
 *
 *    The first field (cmd) of the message header holds the command
 *    code resp. the type of data to be transmitted. The next field (len)
 *    gives the number of data bytes that follow the header. This number
 *    ranges from 0 to sizeof( MSG_DATA ). The third field (data_csum)
 *    holds a checksum of all data bytes and the last field of the header
 *    finally holds the checksum of the header itself.
 *
 ***************************************************************************/

/**
 * @brief GPS epoch bias from ordinary time_t epoch
 *
 * The Unix time_t epoch is usually 1970-01-01 00:00 whereas
 * the GPS epoch is 1980-01-06 00:00, so the difference is 10 years,
 * plus 2 days due to leap years (1972 and 1976), plus the difference
 * of the day-of-month (6 - 1), so:<br>
 *
 * time_t t = ( gps_week * ::SECS_PER_WEEK ) + sec_of_week + ::GPS_SEC_BIAS
 */
#define GPS_SEC_BIAS   315964800UL     // ( ( ( 10UL * 365UL ) + 2 + 5 ) * SECS_PER_DAY )


#ifndef _COM_HS_DEFINED
  /**
   * @brief Enumeration of handshake modes
   */
  enum COM_HANSHAKE_MODES { HS_NONE, HS_XONXOFF, HS_RTSCTS, N_COM_HS };
  #define _COM_HS_DEFINED
#endif

#ifndef _COM_PARM_DEFINED
  /**
   * @brief A data type to configure a serial port's baud rate
   *
   * @see ::MBG_BAUD_RATES
   */
  typedef int32_t BAUD_RATE;

  /**
   * @brief Indices used to identify a parameter in the framing string
   *
   * @see ::MBG_FRAMING_STRS
   */
  enum MBG_FRAMING_STR_IDXS { F_DBITS, F_PRTY, F_STBITS };

  /**
   * @brief A structure to store the configuration of a serial port
   */
  typedef struct
  {
    BAUD_RATE baud_rate;  ///< transmission speed, e.g. 19200L, see ::MBG_BAUD_RATES
    char framing[4];      ///< ASCIIZ framing string, e.g. "8N1" or "7E2", see ::MBG_FRAMING_STRS
    int16_t handshake;    ///< handshake mode, yet only ::HS_NONE supported

  } COM_PARM;

  #define _COM_PARM_DEFINED
#endif


/**
 * @brief Enumeration of modes supported for time string transmission
 *
 * This determines e.g. at which point in time a string starts
 * to be transmitted via the serial port.
 * Used with ::PORT_SETTINGS::mode.
 *
 * @see ::STR_MODE_MASKS
 */
enum STR_MODES
{
  STR_ON_REQ,     ///< transmission on request by received '?' character only
  STR_PER_SEC,    ///< transmission automatically if second changes
  STR_PER_MIN,    ///< transmission automatically if minute changes
  STR_AUTO,       ///< transmission automatically if required, e.g. on capture event
  STR_ON_REQ_SEC, ///< transmission if second changes and a request has been received before
  N_STR_MODE      ///< the number of known modes
};


/**
 * The number of serial ports which are at least available
 * even with very old GPS receiver models. For devices providing
 * a ::RECEIVER_INFO structure the number of provided COM ports
 * is available in ::RECEIVER_INFO::n_com_ports.
 */
#define DEFAULT_N_COM   2


/**
 * @brief A The structure used to store the configuration of two serial ports
 *
 * @deprecated This structure is deprecated, ::PORT_SETTINGS and related structures
 * should be used instead, if supported by the device.
 */
typedef struct
{
  COM_PARM com[DEFAULT_N_COM];    ///< COM0 and COM1 settings
  uint8_t mode[DEFAULT_N_COM];    ///< COM0 and COM1 output mode

} PORT_PARM;


/**
 * @brief The type of a GPS command code
 *
 * @see ::GPS_CMD_CODES
 */
typedef uint16_t GPS_CMD;


/**
 * @brief Control codes to be or'ed with a particular command/type code
 */
enum GPS_CMD_CTRL_CODES
{
  GPS_REQACK = 0x8000,   ///< to device: request acknowledge
  GPS_ACK    = 0x4000,   ///< from device: acknowledge a command
  GPS_NACK   = 0x2000,   ///< from device: error evaluating a command
};

#define GPS_CTRL_MSK  0xF000   ///< bit mask of all ::GPS_CMD_CTRL_CODES


/**
 * @brief Command codes for the binary protocol
 *
 * These codes specify commands and associated data types used by Meinberg's
 * binary protocol to exchange data with a device via serial port, direct USB,
 * or socket I/O.
 *
 * Some commands and associated data structures can be read (r) from a device, others
 * can be written (w) to the device, and some can also be sent automatically (a) by
 * a device after a ::GPS_AUTO_ON command has been sent to the device.
 * The individual command codes are marked with (rwa) accordingly, where '-' is used
 * to indicate that a particular mode is not supported.
 *
 * @note Not all command code are supported by all devices.
 * See the hints for a particular command.
 *
 * @note If ::GPS_ALM, ::GPS_EPH or a code named ..._IDX is sent to retrieve
 * some data from a device then an uint16_t parameter must be also supplied
 * in order to specify the index number of the data set to be returned.
 * The valid index range depends on the command code.
 * For ::GPS_ALM and ::GPS_EPH the index is the SV number which may be 0 or
 * ::MIN_SVNO_GPS to ::MAX_SVNO_GPS. If the number is 0 then all ::N_SVNO_GPS
 * almanacs or ephemeris data structures are returned.
 *
 * @see ::GPS_CMD_CODES_TABLE
 */
enum GPS_CMD_CODES
{ /* system data */
  GPS_AUTO_ON = 0x000,  ///< (-w-) no data, enable auto-msgs from device
  GPS_AUTO_OFF,         ///< (-w-) no data, disable auto-msgs from device
  GPS_SW_REV,           ///< (r--) deprecated, ::SW_REV, software revision, use only if ::GPS_RECEIVER_INFO not supp.
  GPS_BVAR_STAT,        ///< (r--) ::BVAR_STAT, status of buffered variables, only if ::GPS_MODEL_HAS_BVAR_STAT
  GPS_TIME,             ///< (-wa) ::TTM, current time or capture, or init board time
  GPS_POS_XYZ,          ///< (rw-) ::XYZ, current position in ECEF coordinates, only if ::GPS_MODEL_HAS_POS_XYZ
  GPS_POS_LLA,          ///< (rw-) ::LLA, current position in geographic coordinates, only if ::GPS_MODEL_HAS_POS_LLA
  GPS_TZDL,             ///< (rw-) ::TZDL, time zone / daylight saving, only if ::GPS_MODEL_HAS_TZDL
  GPS_PORT_PARM,        ///< (rw-) deprecated, ::PORT_PARM, use ::PORT_SETTINGS etc. if ::GPS_RECEIVER_INFO supported
  GPS_SYNTH,            ///< (rw-) ::SYNTH, synthesizer settings, only if ::GPS_HAS_SYNTH
  GPS_ANT_INFO,         ///< (r-a) ::ANT_INFO, time diff after antenna disconnect, only if ::GPS_MODEL_HAS_ANT_INFO
  GPS_UCAP,             ///< (r-a) ::TTM, user capture events, only if ::RECEIVER_INFO::n_ucaps > 0

  /* GPS data */
  GPS_CFGH = 0x100,     ///< (rw-) ::CFGH, SVs' configuration and health codes
  GPS_ALM,              ///< (rw-) req: uint16_t SV num, ::SV_ALM, one SV's almanac
  GPS_EPH,              ///< (rw-) req: uint16_t SV num, ::SV_EPH, one SV's ephemeris
  GPS_UTC,              ///< (rw-) ::UTC, GPS %UTC correction parameters
  GPS_IONO,             ///< (rw-) ::IONO, GPS ionospheric correction parameters
  GPS_ASCII_MSG         ///< (r--) ::ASCII_MSG, the GPS ASCII message
};


#ifndef _CSUM_DEFINED
  typedef uint16_t CSUM;  /* checksum used by some structures stored in non-volatile memory */
  #define _CSUM_DEFINED
#endif


/**
 * @brief The header of a binary message.
 */
typedef struct
{
  GPS_CMD cmd;      ///< see ::GPS_CMD_CODES
  uint16_t len;     ///< length of the data portion appended after the header
  CSUM data_csum;   ///< checksum of the data portion appended after the header
  CSUM hdr_csum;    ///< checksum of the preceding header bytes

} GPS_MSG_HDR;


#define GPS_ID_STR_LEN      16
#define GPS_ID_STR_SIZE     ( GPS_ID_STR_LEN + 1 )

/**
 * @brief Software revision information
 *
 * Contains a software revision code, plus an optional
 * identifier for a customized version.
 */
typedef struct
{
  uint16_t code;               ///< Version number, e.g. 0x0120 means v1.20
  char name[GPS_ID_STR_SIZE];  ///< Optional string identifying a customized version
  uint8_t reserved;            ///< Reserved field to yield even structure size

} SW_REV;


/**
 * @brief GNSS satellite numbers
 *
 * @todo: Check if MAX_SVNO_GLN is 94 instead of 95, and thus
 *        N_SVNO_GLN is 30 instead of 31, as reported by Wikipedia.
 */
enum GNSS_SVNOS
{
  MIN_SVNO_GPS = 1,       ///< min. GPS satellite PRN number
  MAX_SVNO_GPS = 32,      ///< max. GPS satellite PRN number
  N_SVNO_GPS = 32,        ///< max. number of active GPS satellites

  MIN_SVNO_WAAS = 33,     ///< min. WAAS satellite number
  MAX_SVNO_WAAS = 64,     ///< max. WAAS satellite number
  N_SVNO_WAAS = 32,       ///< max. number of active WAAS satellites

  MIN_SVNO_GLONASS = 65,  ///< min. Glonass satellite number (64 + sat slot ID)
  MAX_SVNO_GLONASS = 95,  ///< max. Glonass satellite number (64 + sat slot ID)
  N_SVNO_GLONASS = 31     ///< max. number of active Glonass satellites
};


typedef uint16_t SVNO;    ///< the number of an SV (Space Vehicle, i.e. satellite)
typedef uint16_t HEALTH;  ///< an SV's 6 bit health code
typedef uint16_t CFG;     ///< an SV's 4 bit configuration code
typedef uint16_t IOD;     ///< Issue-Of-Data code


/**
 * @brief Status flags of battery buffered data
 *
 * Related to data received from the satellites, or data derived thereof.
 *
 * All '0' means OK, single bits set to '1' indicate
 * the associated type of GPS data is not available.
 *
 * @see ::BVAR_FLAGS
 */
typedef uint16_t BVAR_STAT;

#define _mbg_swab_bvar_stat( _p )  _mbg_swab16( (_p) )


/**
 * @brief Enumeration of flag bits used to define ::BVAR_FLAGS
 *
 * For each bit which is set this means the associated data set in
 * non-volatile memory is not available, or incomplete.
 * Most data sets will just be re-collected from the data streams sent
 * by the satellites. However, the receiver position has usually been
 * computed earlier during normal operation, and will be re-computed
 * when a sufficient number of satellites can be received.
 *
 * @see ::BVAR_STAT
 * @see ::BVAR_FLAGS
 * @see ::BVAR_FLAG_NAMES
 */
enum BVAR_FLAG_BITS
{
  BVAR_BIT_CFGH_INVALID,      ///< Satellite configuration and health parameters incomplete
  BVAR_BIT_ALM_NOT_COMPLETE,  ///< Almanac parameters incomplete
  BVAR_BIT_UTC_INVALID,       ///< %UTC offset parameters incomplete
  BVAR_BIT_IONO_INVALID,      ///< Ionospheric correction parameters incomplete
  BVAR_BIT_RCVR_POS_INVALID,  ///< No valid receiver position available
  N_BVAR_BIT                  ///< number of defined ::BVAR_STAT bits
};


/**
 * @brief Bit masks associated with ::BVAR_FLAG_BITS
 *
 * Used with ::BVAR_STAT.
 *
 * @see ::BVAR_STAT
 * @see ::BVAR_FLAG_BITS
 * @see ::BVAR_FLAG_NAMES
 */
enum BVAR_FLAGS
{
  BVAR_CFGH_INVALID     = ( 1UL << BVAR_BIT_CFGH_INVALID ),      ///< see ::BVAR_BIT_CFGH_INVALID
  BVAR_ALM_NOT_COMPLETE = ( 1UL << BVAR_BIT_ALM_NOT_COMPLETE ),  ///< see ::BVAR_BIT_ALM_NOT_COMPLETE
  BVAR_UTC_INVALID      = ( 1UL << BVAR_BIT_UTC_INVALID ),       ///< see ::BVAR_BIT_UTC_INVALID
  BVAR_IONO_INVALID     = ( 1UL << BVAR_BIT_IONO_INVALID ),      ///< see ::BVAR_BIT_IONO_INVALID
  BVAR_RCVR_POS_INVALID = ( 1UL << BVAR_BIT_RCVR_POS_INVALID ),  ///< see ::BVAR_BIT_RCVR_POS_INVALID
};


/**
 * @brief A structure used to hold time in GPS format
 *
 * Date and time refer to the linear time scale defined by GPS, with
 * the epoch starting at %UTC midnight at the beginning of January 6, 1980.
 *
 * GPS time is counted by the week numbers since the epoch, plus second
 * of the week, plus fraction of the second. The week number transmitted
 * by the satellites rolls over from 1023 to 0, but Meinberg devices
 * just continue to count the weeks beyond the 1024 week limit to keep
 * the receiver's internal time.
 *
 * %UTC time differs from GPS time since a number of leap seconds have
 * been inserted in the %UTC time scale after the GPS epoche. The number
 * of leap seconds is disseminated by the satellites using the ::UTC
 * parameter set, which also provides info on pending leap seconds.
 */
typedef struct
{
  uint16_t wn;     ///< the week number since GPS has been installed
  uint32_t sec;    ///< the second of that week
  uint32_t tick;   ///< fractions of a second, 1/::RECEIVER_INFO::ticks_per_sec units

} T_GPS;


/**
 * @brief Local date and time computed from GPS time
 *
 * The current number of leap seconds have to be added to get %UTC
 * from GPS time. Additional corrections could have been made according
 * to the time zone/daylight saving parameters ::TZDL defined by the user.
 * The status field can be checked to see which corrections
 * have actually been applied.
 *
 * @note Conversion from GPS time to %UTC and/or local time can only be
 * done if some valid ::UTC correction parameters are available in the
 * receiver's non-volatile memory.
 */
typedef struct
{
  int16_t year;           ///< year number, 0..9999
  int8_t month;           ///< month, 1..12
  int8_t mday;            ///< day of month, 1..31
  int16_t yday;           ///< day of year, 1..365, or 366 in case of leap year
  int8_t wday;            ///< day of week, 0..6 == Sun..Sat
  int8_t hour;            ///< hours, 0..23
  int8_t min;             ///< minutes, 0..59
  int8_t sec;             ///< seconds, 0..59, or 60 in case of inserted leap second
  int32_t frac;           ///< fractions of a second, 1/::RECEIVER_INFO::ticks_per_sec units
  int32_t offs_from_utc;  ///< local time offset from %UTC [sec]
  uint16_t status;        ///< status flags, see ::TM_GPS_STATUS_BIT_MASKS

} TM_GPS;



/**
 * @brief Status flag bits used to define ::TM_GPS_STATUS_BIT_MASKS
 *
 * These bits report info on the time conversion from GPS time to %UTC
 * and/or local time as well as device status info.
 *
 * @see ::TM_GPS_STATUS_BIT_MASKS
 */
enum TM_GPS_STATUS_BITS
{
  TM_BIT_UTC,          ///< %UTC correction has been made
  TM_BIT_LOCAL,        ///< %UTC has been converted to local time according to ::TZDL settings
  TM_BIT_DL_ANN,       ///< state of daylight saving is going to change
  TM_BIT_DL_ENB,       ///< daylight saving is in effect
  TM_BIT_LS_ANN,       ///< leap second pending
  TM_BIT_LS_ENB,       ///< current second is leap second
  TM_BIT_LS_ANN_NEG,   ///< set in addition to ::TM_BIT_LS_ANN if leap sec is negative
  TM_BIT_INVT,         ///< invalid time, e.g. if RTC battery bas been empty

  TM_BIT_EXT_SYNC,     ///< synchronized externally
  TM_BIT_HOLDOVER,     ///< in holdover mode after previous synchronization
  TM_BIT_ANT_SHORT,    ///< antenna cable short circuited
  TM_BIT_NO_WARM,      ///< OCXO has not warmed up
  TM_BIT_ANT_DISCONN,  ///< antenna currently disconnected
  TM_BIT_SYN_FLAG,     ///< TIME_SYN output is low
  TM_BIT_NO_SYNC,      ///< time sync actually not verified
  TM_BIT_NO_POS        ///< position actually not verified, LOCK LED off
};


/**
 * @brief Status flag masks used with ::TM_GPS::status
 *
 * These bits report info on the time conversion from GPS time to %UTC
 * and/or local time as well as device status info.
 *
 * @see ::TM_GPS_STATUS_BITS
 */
enum TM_GPS_STATUS_BIT_MASKS
{
  TM_UTC         = ( 1UL << TM_BIT_UTC ),          ///< see ::TM_BIT_UTC
  TM_LOCAL       = ( 1UL << TM_BIT_LOCAL ),        ///< see ::TM_BIT_LOCAL
  TM_DL_ANN      = ( 1UL << TM_BIT_DL_ANN ),       ///< see ::TM_BIT_DL_ANN
  TM_DL_ENB      = ( 1UL << TM_BIT_DL_ENB ),       ///< see ::TM_BIT_DL_ENB
  TM_LS_ANN      = ( 1UL << TM_BIT_LS_ANN ),       ///< see ::TM_BIT_LS_ANN
  TM_LS_ENB      = ( 1UL << TM_BIT_LS_ENB ),       ///< see ::TM_BIT_LS_ENB
  TM_LS_ANN_NEG  = ( 1UL << TM_BIT_LS_ANN_NEG ),   ///< see ::TM_BIT_LS_ANN_NEG
  TM_INVT        = ( 1UL << TM_BIT_INVT ),         ///< see ::TM_BIT_INVT

  TM_EXT_SYNC    = ( 1UL << TM_BIT_EXT_SYNC ),     ///< see ::TM_BIT_EXT_SYNC
  TM_HOLDOVER    = ( 1UL << TM_BIT_HOLDOVER ),     ///< see ::TM_BIT_HOLDOVER
  TM_ANT_SHORT   = ( 1UL << TM_BIT_ANT_SHORT ),    ///< see ::TM_BIT_ANT_SHORT
  TM_NO_WARM     = ( 1UL << TM_BIT_NO_WARM ),      ///< see ::TM_BIT_NO_WARM
  TM_ANT_DISCONN = ( 1UL << TM_BIT_ANT_DISCONN ),  ///< see ::TM_BIT_ANT_DISCONN
  TM_SYN_FLAG    = ( 1UL << TM_BIT_SYN_FLAG ),     ///< see ::TM_BIT_SYN_FLAG
  TM_NO_SYNC     = ( 1UL << TM_BIT_NO_SYNC ),      ///< see ::TM_BIT_NO_SYNC
  TM_NO_POS      = ( 1UL << TM_BIT_NO_POS )        ///< see ::TM_BIT_NO_POS
};


/**
 * @brief A structure used to transmit information on date and time
 *
 * This structure can be used to transfer the current time, in which
 * case the channel field has to be set to -1, or an event capture time
 * retrieved from the on-board FIFO, in which case the channel field
 * contains the index of the time capture input, e.g. 0 or 1.
 */
typedef struct
{
  int16_t channel;  ///< -1: the current on-board time; >= 0 the capture channel number
  T_GPS t;          ///< time in GPS scale and format
  TM_GPS tm;        ///< time converted to %UTC and/or local time according to ::TZDL settings

} TTM;



/* Two types of variables used to store a position. Type XYZ is */
/* used with a position in earth centered, earth fixed (ECEF) */
/* coordinates whereas type LLA holds such a position converted */
/* to geographic coordinates as defined by WGS84 (World Geodetic */
/* System from 1984). */

/**
 * @brief Sequence and number of components of a cartesian position
 */
enum XYZ_FIELDS { XP, YP, ZP, N_XYZ };  // x, y, z

/**
 * @brief A position in cartesian coordinates
 *
 * Usually earth centered, earth fixed (ECEF) coordinates,
 * in [m].
 *
 * @note In the original code this is an array of double.
 *
 * @see ::XYZ_FIELDS
 */
typedef l_fp XYZ[N_XYZ];


/**
 * @brief Sequence and number of components of a geographic position
 */
enum LLA_FIELDS { LAT, LON, ALT, N_LLA };  /* latitude, longitude, altitude */

/**
 * @brief A geographic position based on latitude, longitude, and altitude
 *
 * The geographic position associated to specific cartesian coordinates
 * depends on the characteristics of the ellipsoid used for the computation,
 * the so-called geographic datum. GPS uses the WGS84 (World Geodetic System
 * from 1984) ellipsoid by default.
 *
 * lon, lat in [rad], alt in [m]
 *
 * @note In the original code this is an array of double.
 *
 * @see ::LLA_FIELDS
 */
typedef l_fp LLA[N_LLA];


/**
 * @defgroup group_synth Synthesizer parameters
 *
 * Synthesizer frequency is expressed as a
 * four digit decimal number (freq) to be multiplied by 0.1 Hz and an
 * base 10 exponent (range). If the effective frequency is less than
 * 10 kHz its phase is synchronized corresponding to the variable phase.
 * Phase may be in a range from -360 deg to +360 deg with a resolution
 * of 0.1 deg, so the resulting numbers to be stored are in a range of
 * -3600 to +3600.
 *
 * Example:<br>
 * Assume the value of freq is 2345 (decimal) and the value of phase is 900.
 * If range == 0 the effective frequency is 234.5 Hz with a phase of +90 deg.
 * If range == 1 the synthesizer will generate a 2345 Hz output frequency
 * and so on.
 *
 * Limitations:<br>
 * If freq == 0 the synthesizer is disabled. If range == 0 the least
 * significant digit of freq is limited to 0, 3, 5 or 6. The resulting
 * frequency is shown in the examples below:
 *    - freq == 1230  -->  123.0 Hz
 *    - freq == 1233  -->  123 1/3 Hz (real 1/3 Hz, NOT 123.3 Hz)
 *    - freq == 1235  -->  123.5 Hz
 *    - freq == 1236  -->  123 2/3 Hz (real 2/3 Hz, NOT 123.6 Hz)
 *
 * If range == ::MAX_SYNTH_RANGE the value of freq must not exceed 1000, so
 * the output frequency is limited to 10 MHz (see ::MAX_SYNTH_FREQ_VAL).
 *
 * @{ */

#define N_SYNTH_FREQ_DIGIT  4    ///< number of digits to edit
#define MAX_SYNTH_FREQ   1000    ///< if range == ::MAX_SYNTH_RANGE

#define MIN_SYNTH_RANGE     0
#define MAX_SYNTH_RANGE     5
#define N_SYNTH_RANGE       ( MAX_SYNTH_RANGE - MIN_SYNTH_RANGE + 1 )

#define N_SYNTH_PHASE_DIGIT  4
#define MAX_SYNTH_PHASE      3600


#define MAX_SYNTH_FREQ_EDIT  9999  ///< max sequence of digits when editing


/**
 * @brief The maximum frequency that can be configured for the synthesizer
 */
#define MAX_SYNTH_FREQ_VAL   10000000UL     ///< 10 MHz
/*   == MAX_SYNTH_FREQ * 10^(MAX_SYNTH_RANGE-1) */

/**
 * @brief The synthesizer's phase is only be synchronized if the frequency is below this limit
 */
#define SYNTH_PHASE_SYNC_LIMIT   10000UL    ///< 10 kHz

/**
 * A Macro used to determine the position of the decimal point
 * when printing the synthesizer frequency as 4 digit value
 */
#define _synth_dp_pos_from_range( _r ) \
  ( ( ( N_SYNTH_RANGE - (_r) ) % ( N_SYNTH_FREQ_DIGIT - 1 ) ) + 1 )

/**
 * @brief Synthesizer frequency units
 *
 * An initializer for commonly displayed synthesizer frequency units
 * (::N_SYNTH_RANGE strings)
 */
#define DEFAULT_FREQ_RANGES \
{                           \
  "Hz",                     \
  "kHz",                    \
  "kHz",                    \
  "kHz",                    \
  "MHz",                    \
  "MHz",                    \
}



/**
 * @brief Synthesizer configuration parameters
 */
typedef struct
{
  int16_t freq;    ///< four digits used; scale: 0.1 Hz; e.g. 1234 -> 123.4 Hz
  int16_t range;   ///< scale factor for freq; 0..::MAX_SYNTH_RANGE
  int16_t phase;   ///< -::MAX_SYNTH_PHASE..+::MAX_SYNTH_PHASE; >0 -> pulses later

} SYNTH;

#define _mbg_swab_synth( _p )   \
{                               \
  _mbg_swab16( &(_p)->freq );   \
  _mbg_swab16( &(_p)->range );  \
  _mbg_swab16( &(_p)->phase );  \
}


/**
 * @brief Enumeration of synthesizer states
 */
enum SYNTH_STATES
{
  SYNTH_DISABLED,   ///< disbled by cfg, i.e. freq == 0.0
  SYNTH_OFF,        ///< not enabled after power-up
  SYNTH_FREE,       ///< enabled, but not synchronized
  SYNTH_DRIFTING,   ///< has initially been sync'd, but now running free
  SYNTH_SYNC,       ///< fully synchronized
  N_SYNTH_STATE     ///< the number of known states
};


/**
 * @brief A structure used to report the synthesizer state
 */
typedef struct
{
  uint8_t state;     ///< state code as enumerated in ::SYNTH_STATES
  uint8_t flags;     ///< reserved, currently always 0

} SYNTH_STATE;

#define _mbg_swab_synth_state( _p )  _nop_macro_fnc()

#define SYNTH_FLAG_PHASE_IGNORED  0x01

/** @} defgroup group_synth */



/**
 * @defgroup group_tzdl Time zone / daylight saving parameters
 *
 * Example: <br>
 * For automatic daylight saving enable/disable in Central Europe,
 * the variables are to be set as shown below: <br>
 *   - offs = 3600L           one hour from %UTC
 *   - offs_dl = 3600L        one additional hour if daylight saving enabled
 *   - tm_on = first Sunday from March 25, 02:00:00h ( year |= ::DL_AUTO_FLAG )
 *   - tm_off = first Sunday from October 25, 03:00:00h ( year |= ::DL_AUTO_FLAG )
 *   - name[0] == "CET  "     name if daylight saving not enabled
 *   - name[1] == "CEST "     name if daylight saving is enabled
 *
 * @{ */

/**
 * @brief The name of a time zone
 *
 * @note Up to 5 printable characters, plus trailing zero
 */
typedef char TZ_NAME[6];

/**
 * @brief Time zone / daylight saving parameters
 *
 * This structure is used to specify how a device converts on-board %UTC
 * to local time, including computation of beginning and end of daylight
 * saving time (DST), if required.
 *
 * @note The ::TZDL structure contains members of type ::TM_GPS to specify
 * the times for beginning and end of DST. However, the ::TM_GPS::frac,
 * ::TM_GPS::offs_from_utc, and ::TM_GPS::status fields of these ::TZDL::tm_on
 * and ::TZDL::tm_off members are ignored for the conversion to local time,
 * and thus should be 0.
 */
typedef struct
{
  int32_t offs;      ///< standard offset from %UTC to local time [sec]
  int32_t offs_dl;   ///< additional offset if daylight saving enabled [sec]
  TM_GPS tm_on;      ///< date/time when daylight saving starts
  TM_GPS tm_off;     ///< date/time when daylight saving ends
  TZ_NAME name[2];   ///< names without and with daylight saving enabled

} TZDL;

/**
 * @brief A flag indicating automatic computation of DST
 *
 * If this flag is or'ed to the year numbers in ::TZDL::tm_on and ::TZDL::tm_off
 * then daylight saving is computed automatically year by year.
 */
#define DL_AUTO_FLAG  0x8000

/** @} defgroup group_tzdl */



/**
 * @brief Antenna status and error at reconnect information
 *
 * The structure below reflects the status of the antenna,
 * the times of last disconnect/reconnect, and the board's
 * clock offset when it has synchronized again after the
 * disconnection interval.
 *
 * @note ::ANT_INFO::status changes back to ::ANT_RECONN only
 * after the antenna has been reconnected <b>and</b> the
 * receiver has re-synchronized to the satellite signal.
 * In this case ::ANT_INFO::delta_t reports the time offset
 * before resynchronization, i.e. how much the internal
 * time has drifted while the antenna was disconnected.
 */
typedef struct
{
  int16_t status;      ///< current status of antenna, see ::ANT_STATUS_CODES
  TM_GPS tm_disconn;   ///< time of antenna disconnect
  TM_GPS tm_reconn;    ///< time of antenna reconnect
  int32_t delta_t;     ///< clock offs at reconn. time in 1/::RECEIVER_INFO::ticks_per_sec units

} ANT_INFO;


/**
 * @brief Status code used with ::ANT_INFO::status
 */
enum ANT_STATUS_CODES
{
  ANT_INVALID,   ///< No other fields valid since antenna has not yet been disconnected
  ANT_DISCONN,   ///< Antenna is disconnected, tm_reconn and delta_t not yet set
  ANT_RECONN,    ///< Antenna has been disconnect, and receiver sync. after reconnect, so all fields valid
  N_ANT_STATUS_CODES  ///< the number of known status codes
};



/**
 * @brief Summary of configuration and health data of all satellites
 */
typedef struct
{
  CSUM csum;                  ///< checksum of the remaining bytes
  int16_t valid;              ///< flag data are valid

  T_GPS tot_51;               ///< time of transmission, page 51
  T_GPS tot_63;               ///< time of transmission, page 63
  T_GPS t0a;                  ///< complete reference time almanac

  CFG cfg[N_SVNO_GPS];        ///< 4 bit SV configuration code from page 63
  HEALTH health[N_SVNO_GPS];  ///< 6 bit SV health codes from pages 51, 63

} CFGH;



/**
 * @brief GPS %UTC correction parameters
 *
 * %UTC correction parameters basically as sent by the GPS satellites.
 *
 * The csum field is only used by the card's firmware to check the
 * consistency of the structure in non-volatile memory.
 *
 * The field labeled valid indicates if the parameter set is valid, i.e.
 * if it contains data received from the satellites.
 *
 * t0t, A0 and A1 contain fractional correction parameters for the current
 * GPS-%UTC time offset in addition to the whole seconds. This is evaluated
 * by the receivers' firmware to convert GPS time to %UTC time.
 *
 * The delta_tls field contains the current full seconds offset between
 * GPS time and %UTC, which corresponds to the number of leap seconds inserted
 * into the %UTC time scale since GPS was put into operation in January 1980.
 *
 * delta_tlfs holds the number of "future" leap seconds, i.e. the %UTC offset
 * after the next leap second event defined by WNlsf and DNt.
 *
 * The fields WNlsf and DNt specify the GPS week number and the day number
 * in that week for the end of which a leap second has been scheduled.
 *
 * @note: The satellites transmit WNlsf only as a signed 8 bit value, so it
 * can only define a point in time which is +/- 127 weeks off the current time.
 * The firmware tries to expand this based on the current week number, but
 * the result is ambiguous if the leap second occurs or occurred more
 * than 127 weeks in the future or past.
 *
 * So the leap second date should <b>only</b> be evaluated and displayed
 * in a user interface if the fields delta_tls and delta_tlsf have
 * different values, in which case there is indeed a leap second announcement
 * inside the +/- 127 week range.
 *
 * @note In the original code the type of A0 and A1 is double.
 */
typedef struct
{
  CSUM csum;          ///<  Checksum of the remaining bytes
  int16_t valid;      ///<  Flag indicating %UTC parameters are valid

  T_GPS t0t;          ///<  Reference Time %UTC Parameters [wn|sec]
  l_fp A0;            ///<  +- Clock Correction Coefficient 0 [sec]
  l_fp A1;            ///<  +- Clock Correction Coefficient 1 [sec/sec]

  uint16_t WNlsf;     ///<  Week number of nearest leap second
  int16_t DNt;        ///<  The day number at the end of which a leap second occurs
  int8_t delta_tls;   ///<  Current %UTC offset to GPS system time [sec]
  int8_t delta_tlsf;  ///<  Future %UTC offset to GPS system time after next leap second transition [sec]

} UTC;


/**
 * @brief GPS ASCII message
 */
typedef struct
{
  CSUM csum;       ///< checksum of the remaining bytes */
  int16_t valid;   ///< flag data are valid
  char s[23];      ///< 22 chars GPS ASCII message plus trailing zero

} ASCII_MSG;


/**
 * @brief Ephemeris parameters of one specific satellite
 *
 * Needed to compute the position of a satellite at a given time with
 * high precision. Valid for an interval of 4 to 6 hours from start
 * of transmission.
 */
typedef struct
{
  CSUM csum;       ///<    checksum of the remaining bytes
  int16_t valid;   ///<    flag data are valid

  HEALTH health;   ///<    health indication of transmitting SV      [---]
  IOD IODC;        ///<    Issue Of Data, Clock
  IOD IODE2;       ///<    Issue of Data, Ephemeris (Subframe 2)
  IOD IODE3;       ///<    Issue of Data, Ephemeris (Subframe 3)
  T_GPS tt;        ///<    time of transmission
  T_GPS t0c;       ///<    Reference Time Clock                      [---]
  T_GPS t0e;       ///<    Reference Time Ephemeris                  [---]

  l_fp sqrt_A;     ///<    Square Root of semi-major Axis        [sqrt(m)]
  l_fp e;          ///<    Eccentricity                              [---]
  l_fp M0;         ///< +- Mean Anomaly at Ref. Time                 [rad]
  l_fp omega;      ///< +- Argument of Perigee                       [rad]
  l_fp OMEGA0;     ///< +- Longit. of Asc. Node of orbit plane       [rad]
  l_fp OMEGADOT;   ///< +- Rate of Right Ascension               [rad/sec]
  l_fp deltan;     ///< +- Mean Motion Diff. from computed value [rad/sec]
  l_fp i0;         ///< +- Inclination Angle                         [rad]
  l_fp idot;       ///< +- Rate of Inclination Angle             [rad/sec]
  l_fp crc;        ///< +- Cosine Corr. Term to Orbit Radius           [m]
  l_fp crs;        ///< +- Sine Corr. Term to Orbit Radius             [m]
  l_fp cuc;        ///< +- Cosine Corr. Term to Arg. of Latitude     [rad]
  l_fp cus;        ///< +- Sine Corr. Term to Arg. of Latitude       [rad]
  l_fp cic;        ///< +- Cosine Corr. Term to Inclination Angle    [rad]
  l_fp cis;        ///< +- Sine Corr. Term to Inclination Angle      [rad]

  l_fp af0;        ///< +- Clock Correction Coefficient 0            [sec]
  l_fp af1;        ///< +- Clock Correction Coefficient 1        [sec/sec]
  l_fp af2;        ///< +- Clock Correction Coefficient 2      [sec/sec^2]
  l_fp tgd;        ///< +- estimated group delay differential        [sec]

  uint16_t URA;    ///<    predicted User Range Accuracy

  uint8_t L2code;  ///<    code on L2 channel                         [---]
  uint8_t L2flag;  ///<    L2 P data flag                             [---]

} EPH;



/**
 * @brief Almanac parameters of one specific satellite
 *
 * A reduced precision set of parameters used to check if a satellite
 * is in view at a given time. Valid for an interval of more than 7 days
 * from start of transmission.
 */
typedef struct
{
  CSUM csum;       ///<    checksum of the remaining bytes
  int16_t valid;   ///<    flag data are valid

  HEALTH health;   ///<                                               [---]
  T_GPS t0a;       ///<    Reference Time Almanac                     [sec]

  l_fp sqrt_A;     ///<    Square Root of semi-major Axis         [sqrt(m)]
  l_fp e;          ///<    Eccentricity                               [---]

  l_fp M0;         ///< +- Mean Anomaly at Ref. Time                  [rad]
  l_fp omega;      ///< +- Argument of Perigee                        [rad]
  l_fp OMEGA0;     ///< +- Longit. of Asc. Node of orbit plane        [rad]
  l_fp OMEGADOT;   ///< +- Rate of Right Ascension                [rad/sec]
  l_fp deltai;     ///< +-                                            [rad]
  l_fp af0;        ///< +- Clock Correction Coefficient 0             [sec]
  l_fp af1;        ///< +- Clock Correction Coefficient 1         [sec/sec]

} ALM;



/**
 * @brief Ionospheric correction parameters
 */
typedef struct
{
  CSUM csum;       ///<    checksum of the remaining bytes
  int16_t valid;   ///<    flag data are valid

  l_fp alpha_0;    ///<    Ionosph. Corr. Coeff. Alpha 0              [sec]
  l_fp alpha_1;    ///<    Ionosph. Corr. Coeff. Alpha 1          [sec/deg]
  l_fp alpha_2;    ///<    Ionosph. Corr. Coeff. Alpha 2        [sec/deg^2]
  l_fp alpha_3;    ///<    Ionosph. Corr. Coeff. Alpha 3        [sec/deg^3]

  l_fp beta_0;     ///<    Ionosph. Corr. Coeff. Beta 0               [sec]
  l_fp beta_1;     ///<    Ionosph. Corr. Coeff. Beta 1           [sec/deg]
  l_fp beta_2;     ///<    Ionosph. Corr. Coeff. Beta 2         [sec/deg^2]
  l_fp beta_3;     ///<    Ionosph. Corr. Coeff. Beta 3         [sec/deg^3]

} IONO;



void mbg_tm_str (char **, TM_GPS *, int, int);
void mbg_tgps_str (char **, T_GPS *, int);
void get_mbg_header (unsigned char **, GPS_MSG_HDR *);
void put_mbg_header (unsigned char **, GPS_MSG_HDR *);
void get_mbg_sw_rev (unsigned char **, SW_REV *);
void get_mbg_ascii_msg (unsigned char **, ASCII_MSG *);
void get_mbg_svno (unsigned char **, SVNO *);
void get_mbg_health (unsigned char **, HEALTH *);
void get_mbg_cfg (unsigned char **, CFG *);
void get_mbg_tgps (unsigned char **, T_GPS *);
void get_mbg_tm (unsigned char **, TM_GPS *);
void get_mbg_ttm (unsigned char **, TTM *);
void get_mbg_synth (unsigned char **, SYNTH *);
void get_mbg_tzdl (unsigned char **, TZDL *);
void get_mbg_antinfo (unsigned char **, ANT_INFO *);
void get_mbg_cfgh (unsigned char **, CFGH *);
void get_mbg_utc (unsigned char **, UTC *);
void get_mbg_lla (unsigned char **, LLA);
void get_mbg_xyz (unsigned char **, XYZ);
void get_mbg_portparam (unsigned char **, PORT_PARM *);
void get_mbg_eph (unsigned char **, EPH *);
void get_mbg_alm (unsigned char **, ALM *);
void get_mbg_iono (unsigned char **, IONO *);

CSUM mbg_csum (unsigned char *, unsigned int);

#endif
/*
 * History:
 *
 * mbg_gps166.h,v
 * Revision 4.7  2006/06/22 18:41:43  kardel
 * clean up signedness (gcc 4)
 *
 * Revision 4.6  2005/10/07 22:11:56  kardel
 * bounded buffer implementation
 *
 * Revision 4.5.2.1  2005/09/25 10:23:48  kardel
 * support bounded buffers
 *
 * Revision 4.5  2005/06/25 10:58:45  kardel
 * add missing log keywords
 *
 * Revision 4.1  1998/06/12 15:07:30  kardel
 * fixed prototyping
 *
 * Revision 4.0  1998/04/10 19:50:42  kardel
 * Start 4.0 release version numbering
 *
 * Revision 1.1  1998/04/10 19:27:34  kardel
 * initial NTP VERSION 4 integration of PARSE with GPS166 binary support
 *
 * Revision 1.1  1997/10/06 20:55:38  kardel
 * new parse structure
 *
 */
