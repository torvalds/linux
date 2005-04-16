
/*
===============================================================================

This C header file is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://HTTP.CS.Berkeley.EDU/~jhauser/
arithmetic/softfloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these three paragraphs for those parts of
this code that are retained.

===============================================================================
*/

#ifndef __SOFTFLOAT_H__
#define __SOFTFLOAT_H__

/*
-------------------------------------------------------------------------------
The macro `FLOATX80' must be defined to enable the extended double-precision
floating-point format `floatx80'.  If this macro is not defined, the
`floatx80' type will not be defined, and none of the functions that either
input or output the `floatx80' type will be defined.
-------------------------------------------------------------------------------
*/
#define FLOATX80

/*
-------------------------------------------------------------------------------
Software IEC/IEEE floating-point types.
-------------------------------------------------------------------------------
*/
typedef unsigned long int float32;
typedef unsigned long long float64;
typedef struct {
    unsigned short high;
    unsigned long long low;
} floatx80;

/*
-------------------------------------------------------------------------------
Software IEC/IEEE floating-point underflow tininess-detection mode.
-------------------------------------------------------------------------------
*/
extern signed char float_detect_tininess;
enum {
    float_tininess_after_rounding  = 0,
    float_tininess_before_rounding = 1
};

/*
-------------------------------------------------------------------------------
Software IEC/IEEE floating-point rounding mode.
-------------------------------------------------------------------------------
*/
extern signed char float_rounding_mode;
enum {
    float_round_nearest_even = 0,
    float_round_to_zero      = 1,
    float_round_down         = 2,
    float_round_up           = 3
};

/*
-------------------------------------------------------------------------------
Software IEC/IEEE floating-point exception flags.
-------------------------------------------------------------------------------
extern signed char float_exception_flags;
enum {
    float_flag_inexact   =  1,
    float_flag_underflow =  2,
    float_flag_overflow  =  4,
    float_flag_divbyzero =  8,
    float_flag_invalid   = 16
};

ScottB: November 4, 1998
Changed the enumeration to match the bit order in the FPA11.
*/

extern signed char float_exception_flags;
enum {
    float_flag_invalid   =  1,
    float_flag_divbyzero =  2,
    float_flag_overflow  =  4,
    float_flag_underflow =  8,
    float_flag_inexact   = 16
};

/*
-------------------------------------------------------------------------------
Routine to raise any or all of the software IEC/IEEE floating-point
exception flags.
-------------------------------------------------------------------------------
*/
void float_raise( signed char );

/*
-------------------------------------------------------------------------------
Software IEC/IEEE integer-to-floating-point conversion routines.
-------------------------------------------------------------------------------
*/
float32 int32_to_float32( signed int );
float64 int32_to_float64( signed int );
#ifdef FLOATX80
floatx80 int32_to_floatx80( signed int );
#endif

/*
-------------------------------------------------------------------------------
Software IEC/IEEE single-precision conversion routines.
-------------------------------------------------------------------------------
*/
signed int float32_to_int32( float32 );
signed int float32_to_int32_round_to_zero( float32 );
float64 float32_to_float64( float32 );
#ifdef FLOATX80
floatx80 float32_to_floatx80( float32 );
#endif

/*
-------------------------------------------------------------------------------
Software IEC/IEEE single-precision operations.
-------------------------------------------------------------------------------
*/
float32 float32_round_to_int( float32 );
float32 float32_add( float32, float32 );
float32 float32_sub( float32, float32 );
float32 float32_mul( float32, float32 );
float32 float32_div( float32, float32 );
float32 float32_rem( float32, float32 );
float32 float32_sqrt( float32 );
char float32_eq( float32, float32 );
char float32_le( float32, float32 );
char float32_lt( float32, float32 );
char float32_eq_signaling( float32, float32 );
char float32_le_quiet( float32, float32 );
char float32_lt_quiet( float32, float32 );
char float32_is_signaling_nan( float32 );

/*
-------------------------------------------------------------------------------
Software IEC/IEEE double-precision conversion routines.
-------------------------------------------------------------------------------
*/
signed int float64_to_int32( float64 );
signed int float64_to_int32_round_to_zero( float64 );
float32 float64_to_float32( float64 );
#ifdef FLOATX80
floatx80 float64_to_floatx80( float64 );
#endif

/*
-------------------------------------------------------------------------------
Software IEC/IEEE double-precision operations.
-------------------------------------------------------------------------------
*/
float64 float64_round_to_int( float64 );
float64 float64_add( float64, float64 );
float64 float64_sub( float64, float64 );
float64 float64_mul( float64, float64 );
float64 float64_div( float64, float64 );
float64 float64_rem( float64, float64 );
float64 float64_sqrt( float64 );
char float64_eq( float64, float64 );
char float64_le( float64, float64 );
char float64_lt( float64, float64 );
char float64_eq_signaling( float64, float64 );
char float64_le_quiet( float64, float64 );
char float64_lt_quiet( float64, float64 );
char float64_is_signaling_nan( float64 );

#ifdef FLOATX80

/*
-------------------------------------------------------------------------------
Software IEC/IEEE extended double-precision conversion routines.
-------------------------------------------------------------------------------
*/
signed int floatx80_to_int32( floatx80 );
signed int floatx80_to_int32_round_to_zero( floatx80 );
float32 floatx80_to_float32( floatx80 );
float64 floatx80_to_float64( floatx80 );

/*
-------------------------------------------------------------------------------
Software IEC/IEEE extended double-precision rounding precision.  Valid
values are 32, 64, and 80.
-------------------------------------------------------------------------------
*/
extern signed char floatx80_rounding_precision;

/*
-------------------------------------------------------------------------------
Software IEC/IEEE extended double-precision operations.
-------------------------------------------------------------------------------
*/
floatx80 floatx80_round_to_int( floatx80 );
floatx80 floatx80_add( floatx80, floatx80 );
floatx80 floatx80_sub( floatx80, floatx80 );
floatx80 floatx80_mul( floatx80, floatx80 );
floatx80 floatx80_div( floatx80, floatx80 );
floatx80 floatx80_rem( floatx80, floatx80 );
floatx80 floatx80_sqrt( floatx80 );
char floatx80_eq( floatx80, floatx80 );
char floatx80_le( floatx80, floatx80 );
char floatx80_lt( floatx80, floatx80 );
char floatx80_eq_signaling( floatx80, floatx80 );
char floatx80_le_quiet( floatx80, floatx80 );
char floatx80_lt_quiet( floatx80, floatx80 );
char floatx80_is_signaling_nan( floatx80 );

#endif

#endif
