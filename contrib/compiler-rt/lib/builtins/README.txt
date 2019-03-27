Compiler-RT
================================

This directory and its subdirectories contain source code for the compiler
support routines.

Compiler-RT is open source software. You may freely distribute it under the
terms of the license agreement found in LICENSE.txt.

================================

This is a replacement library for libgcc.  Each function is contained
in its own file.  Each function has a corresponding unit test under
test/Unit.

A rudimentary script to test each file is in the file called
test/Unit/test.

Here is the specification for this library:

http://gcc.gnu.org/onlinedocs/gccint/Libgcc.html#Libgcc

Here is a synopsis of the contents of this library:

typedef      int si_int;
typedef unsigned su_int;

typedef          long long di_int;
typedef unsigned long long du_int;

// Integral bit manipulation

di_int __ashldi3(di_int a, si_int b);      // a << b
ti_int __ashlti3(ti_int a, si_int b);      // a << b

di_int __ashrdi3(di_int a, si_int b);      // a >> b  arithmetic (sign fill)
ti_int __ashrti3(ti_int a, si_int b);      // a >> b  arithmetic (sign fill)
di_int __lshrdi3(di_int a, si_int b);      // a >> b  logical    (zero fill)
ti_int __lshrti3(ti_int a, si_int b);      // a >> b  logical    (zero fill)

si_int __clzsi2(si_int a);  // count leading zeros
si_int __clzdi2(di_int a);  // count leading zeros
si_int __clzti2(ti_int a);  // count leading zeros
si_int __ctzsi2(si_int a);  // count trailing zeros
si_int __ctzdi2(di_int a);  // count trailing zeros
si_int __ctzti2(ti_int a);  // count trailing zeros

si_int __ffssi2(si_int a);  // find least significant 1 bit
si_int __ffsdi2(di_int a);  // find least significant 1 bit
si_int __ffsti2(ti_int a);  // find least significant 1 bit

si_int __paritysi2(si_int a);  // bit parity
si_int __paritydi2(di_int a);  // bit parity
si_int __parityti2(ti_int a);  // bit parity

si_int __popcountsi2(si_int a);  // bit population
si_int __popcountdi2(di_int a);  // bit population
si_int __popcountti2(ti_int a);  // bit population

uint32_t __bswapsi2(uint32_t a);   // a byteswapped
uint64_t __bswapdi2(uint64_t a);   // a byteswapped

// Integral arithmetic

di_int __negdi2    (di_int a);                         // -a
ti_int __negti2    (ti_int a);                         // -a
di_int __muldi3    (di_int a, di_int b);               // a * b
ti_int __multi3    (ti_int a, ti_int b);               // a * b
si_int __divsi3    (si_int a, si_int b);               // a / b   signed
di_int __divdi3    (di_int a, di_int b);               // a / b   signed
ti_int __divti3    (ti_int a, ti_int b);               // a / b   signed
su_int __udivsi3   (su_int n, su_int d);               // a / b   unsigned
du_int __udivdi3   (du_int a, du_int b);               // a / b   unsigned
tu_int __udivti3   (tu_int a, tu_int b);               // a / b   unsigned
si_int __modsi3    (si_int a, si_int b);               // a % b   signed
di_int __moddi3    (di_int a, di_int b);               // a % b   signed
ti_int __modti3    (ti_int a, ti_int b);               // a % b   signed
su_int __umodsi3   (su_int a, su_int b);               // a % b   unsigned
du_int __umoddi3   (du_int a, du_int b);               // a % b   unsigned
tu_int __umodti3   (tu_int a, tu_int b);               // a % b   unsigned
du_int __udivmoddi4(du_int a, du_int b, du_int* rem);  // a / b, *rem = a % b  unsigned
tu_int __udivmodti4(tu_int a, tu_int b, tu_int* rem);  // a / b, *rem = a % b  unsigned
su_int __udivmodsi4(su_int a, su_int b, su_int* rem);  // a / b, *rem = a % b  unsigned
si_int __divmodsi4(si_int a, si_int b, si_int* rem);   // a / b, *rem = a % b  signed



//  Integral arithmetic with trapping overflow

si_int __absvsi2(si_int a);           // abs(a)
di_int __absvdi2(di_int a);           // abs(a)
ti_int __absvti2(ti_int a);           // abs(a)

si_int __negvsi2(si_int a);           // -a
di_int __negvdi2(di_int a);           // -a
ti_int __negvti2(ti_int a);           // -a

si_int __addvsi3(si_int a, si_int b);  // a + b
di_int __addvdi3(di_int a, di_int b);  // a + b
ti_int __addvti3(ti_int a, ti_int b);  // a + b

si_int __subvsi3(si_int a, si_int b);  // a - b
di_int __subvdi3(di_int a, di_int b);  // a - b
ti_int __subvti3(ti_int a, ti_int b);  // a - b

si_int __mulvsi3(si_int a, si_int b);  // a * b
di_int __mulvdi3(di_int a, di_int b);  // a * b
ti_int __mulvti3(ti_int a, ti_int b);  // a * b


// Integral arithmetic which returns if overflow

si_int __mulosi4(si_int a, si_int b, int* overflow);  // a * b, overflow set to one if result not in signed range
di_int __mulodi4(di_int a, di_int b, int* overflow);  // a * b, overflow set to one if result not in signed range
ti_int __muloti4(ti_int a, ti_int b, int* overflow);  // a * b, overflow set to
 one if result not in signed range


//  Integral comparison: a  < b -> 0
//                       a == b -> 1
//                       a  > b -> 2

si_int __cmpdi2 (di_int a, di_int b);
si_int __cmpti2 (ti_int a, ti_int b);
si_int __ucmpdi2(du_int a, du_int b);
si_int __ucmpti2(tu_int a, tu_int b);

//  Integral / floating point conversion

di_int __fixsfdi(      float a);
di_int __fixdfdi(     double a);
di_int __fixxfdi(long double a);

ti_int __fixsfti(      float a);
ti_int __fixdfti(     double a);
ti_int __fixxfti(long double a);
uint64_t __fixtfdi(long double input);  // ppc only, doesn't match documentation

su_int __fixunssfsi(      float a);
su_int __fixunsdfsi(     double a);
su_int __fixunsxfsi(long double a);

du_int __fixunssfdi(      float a);
du_int __fixunsdfdi(     double a);
du_int __fixunsxfdi(long double a);

tu_int __fixunssfti(      float a);
tu_int __fixunsdfti(     double a);
tu_int __fixunsxfti(long double a);
uint64_t __fixunstfdi(long double input);  // ppc only

float       __floatdisf(di_int a);
double      __floatdidf(di_int a);
long double __floatdixf(di_int a);
long double __floatditf(int64_t a);        // ppc only

float       __floattisf(ti_int a);
double      __floattidf(ti_int a);
long double __floattixf(ti_int a);

float       __floatundisf(du_int a);
double      __floatundidf(du_int a);
long double __floatundixf(du_int a);
long double __floatunditf(uint64_t a);     // ppc only

float       __floatuntisf(tu_int a);
double      __floatuntidf(tu_int a);
long double __floatuntixf(tu_int a);

//  Floating point raised to integer power

float       __powisf2(      float a, si_int b);  // a ^ b
double      __powidf2(     double a, si_int b);  // a ^ b
long double __powixf2(long double a, si_int b);  // a ^ b
long double __powitf2(long double a, si_int b);  // ppc only, a ^ b

//  Complex arithmetic

//  (a + ib) * (c + id)

      float _Complex __mulsc3( float a,  float b,  float c,  float d);
     double _Complex __muldc3(double a, double b, double c, double d);
long double _Complex __mulxc3(long double a, long double b,
                              long double c, long double d);
long double _Complex __multc3(long double a, long double b,
                              long double c, long double d); // ppc only

//  (a + ib) / (c + id)

      float _Complex __divsc3( float a,  float b,  float c,  float d);
     double _Complex __divdc3(double a, double b, double c, double d);
long double _Complex __divxc3(long double a, long double b,
                              long double c, long double d);
long double _Complex __divtc3(long double a, long double b,
                              long double c, long double d);  // ppc only


//         Runtime support

// __clear_cache() is used to tell process that new instructions have been
// written to an address range.  Necessary on processors that do not have
// a unified instruction and data cache.
void __clear_cache(void* start, void* end);

// __enable_execute_stack() is used with nested functions when a trampoline
// function is written onto the stack and that page range needs to be made
// executable.
void __enable_execute_stack(void* addr);

// __gcc_personality_v0() is normally only called by the system unwinder.
// C code (as opposed to C++) normally does not need a personality function
// because there are no catch clauses or destructors to be run.  But there
// is a C language extension __attribute__((cleanup(func))) which marks local
// variables as needing the cleanup function "func" to be run when the
// variable goes out of scope.  That includes when an exception is thrown,
// so a personality handler is needed.  
_Unwind_Reason_Code __gcc_personality_v0(int version, _Unwind_Action actions,
         uint64_t exceptionClass, struct _Unwind_Exception* exceptionObject,
         _Unwind_Context_t context);

// for use with some implementations of assert() in <assert.h>
void __eprintf(const char* format, const char* assertion_expression,
				const char* line, const char* file);

// for systems with emulated thread local storage
void* __emutls_get_address(struct __emutls_control*);


//   Power PC specific functions

// There is no C interface to the saveFP/restFP functions.  They are helper
// functions called by the prolog and epilog of functions that need to save
// a number of non-volatile float point registers.  
saveFP
restFP

// PowerPC has a standard template for trampoline functions.  This function
// generates a custom trampoline function with the specific realFunc
// and localsPtr values.
void __trampoline_setup(uint32_t* trampOnStack, int trampSizeAllocated, 
                                const void* realFunc, void* localsPtr);

// adds two 128-bit double-double precision values ( x + y )
long double __gcc_qadd(long double x, long double y);  

// subtracts two 128-bit double-double precision values ( x - y )
long double __gcc_qsub(long double x, long double y); 

// multiples two 128-bit double-double precision values ( x * y )
long double __gcc_qmul(long double x, long double y);  

// divides two 128-bit double-double precision values ( x / y )
long double __gcc_qdiv(long double a, long double b);  


//    ARM specific functions

// There is no C interface to the switch* functions.  These helper functions
// are only needed by Thumb1 code for efficient switch table generation.
switch16
switch32
switch8
switchu8

// There is no C interface to the *_vfp_d8_d15_regs functions.  There are
// called in the prolog and epilog of Thumb1 functions.  When the C++ ABI use
// SJLJ for exceptions, each function with a catch clause or destuctors needs
// to save and restore all registers in it prolog and epliog.  But there is 
// no way to access vector and high float registers from thumb1 code, so the 
// compiler must add call outs to these helper functions in the prolog and 
// epilog.
restore_vfp_d8_d15_regs
save_vfp_d8_d15_regs


// Note: long ago ARM processors did not have floating point hardware support.
// Floating point was done in software and floating point parameters were 
// passed in integer registers.  When hardware support was added for floating
// point, new *vfp functions were added to do the same operations but with 
// floating point parameters in floating point registers.

// Undocumented functions

float  __addsf3vfp(float a, float b);   // Appears to return a + b
double __adddf3vfp(double a, double b); // Appears to return a + b
float  __divsf3vfp(float a, float b);   // Appears to return a / b
double __divdf3vfp(double a, double b); // Appears to return a / b
int    __eqsf2vfp(float a, float b);    // Appears to return  one
                                        //     iff a == b and neither is NaN.
int    __eqdf2vfp(double a, double b);  // Appears to return  one
                                        //     iff a == b and neither is NaN.
double __extendsfdf2vfp(float a);       // Appears to convert from
                                        //     float to double.
int    __fixdfsivfp(double a);          // Appears to convert from
                                        //     double to int.
int    __fixsfsivfp(float a);           // Appears to convert from
                                        //     float to int.
unsigned int __fixunssfsivfp(float a);  // Appears to convert from
                                        //     float to unsigned int.
unsigned int __fixunsdfsivfp(double a); // Appears to convert from
                                        //     double to unsigned int.
double __floatsidfvfp(int a);           // Appears to convert from
                                        //     int to double.
float __floatsisfvfp(int a);            // Appears to convert from
                                        //     int to float.
double __floatunssidfvfp(unsigned int a); // Appears to convert from
                                        //     unisgned int to double.
float __floatunssisfvfp(unsigned int a); // Appears to convert from
                                        //     unisgned int to float.
int __gedf2vfp(double a, double b);     // Appears to return __gedf2
                                        //     (a >= b)
int __gesf2vfp(float a, float b);       // Appears to return __gesf2
                                        //     (a >= b)
int __gtdf2vfp(double a, double b);     // Appears to return __gtdf2
                                        //     (a > b)
int __gtsf2vfp(float a, float b);       // Appears to return __gtsf2
                                        //     (a > b)
int __ledf2vfp(double a, double b);     // Appears to return __ledf2
                                        //     (a <= b)
int __lesf2vfp(float a, float b);       // Appears to return __lesf2
                                        //     (a <= b)
int __ltdf2vfp(double a, double b);     // Appears to return __ltdf2
                                        //     (a < b)
int __ltsf2vfp(float a, float b);       // Appears to return __ltsf2
                                        //     (a < b)
double __muldf3vfp(double a, double b); // Appears to return a * b
float __mulsf3vfp(float a, float b);    // Appears to return a * b
int __nedf2vfp(double a, double b);     // Appears to return __nedf2
                                        //     (a != b)
double __negdf2vfp(double a);           // Appears to return -a
float __negsf2vfp(float a);             // Appears to return -a
float __negsf2vfp(float a);             // Appears to return -a
double __subdf3vfp(double a, double b); // Appears to return a - b
float __subsf3vfp(float a, float b);    // Appears to return a - b
float __truncdfsf2vfp(double a);        // Appears to convert from
                                        //     double to float.
int __unorddf2vfp(double a, double b);  // Appears to return __unorddf2
int __unordsf2vfp(float a, float b);    // Appears to return __unordsf2


Preconditions are listed for each function at the definition when there are any.
Any preconditions reflect the specification at
http://gcc.gnu.org/onlinedocs/gccint/Libgcc.html#Libgcc.

Assumptions are listed in "int_lib.h", and in individual files.  Where possible
assumptions are checked at compile time.
