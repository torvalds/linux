/* ===-------- intrin.h ---------------------------------------------------===
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *===-----------------------------------------------------------------------===
 */

/* Only include this if we're compiling for the windows platform. */
#ifndef _MSC_VER
#include_next <intrin.h>
#else

#ifndef __INTRIN_H
#define __INTRIN_H

/* First include the standard intrinsics. */
#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

#if defined(__arm__)
#include <armintr.h>
#endif

#if defined(__aarch64__)
#include <arm64intr.h>
#endif

/* For the definition of jmp_buf. */
#if __STDC_HOSTED__
#include <setjmp.h>
#endif

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__))

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__MMX__)
/* And the random ones that aren't in those files. */
__m64 _m_from_float(float);
float _m_to_float(__m64);
#endif

/* Other assorted instruction intrinsics. */
void __addfsbyte(unsigned long, unsigned char);
void __addfsdword(unsigned long, unsigned long);
void __addfsword(unsigned long, unsigned short);
void __code_seg(const char *);
static __inline__
void __cpuid(int[4], int);
static __inline__
void __cpuidex(int[4], int, int);
static __inline__
__int64 __emul(int, int);
static __inline__
unsigned __int64 __emulu(unsigned int, unsigned int);
unsigned int __getcallerseflags(void);
static __inline__
void __halt(void);
unsigned char __inbyte(unsigned short);
void __inbytestring(unsigned short, unsigned char *, unsigned long);
void __incfsbyte(unsigned long);
void __incfsdword(unsigned long);
void __incfsword(unsigned long);
unsigned long __indword(unsigned short);
void __indwordstring(unsigned short, unsigned long *, unsigned long);
void __int2c(void);
void __invlpg(void *);
unsigned short __inword(unsigned short);
void __inwordstring(unsigned short, unsigned short *, unsigned long);
void __lidt(void *);
unsigned __int64 __ll_lshift(unsigned __int64, int);
__int64 __ll_rshift(__int64, int);
static __inline__
void __movsb(unsigned char *, unsigned char const *, size_t);
static __inline__
void __movsd(unsigned long *, unsigned long const *, size_t);
static __inline__
void __movsw(unsigned short *, unsigned short const *, size_t);
static __inline__
void __nop(void);
void __nvreg_restore_fence(void);
void __nvreg_save_fence(void);
void __outbyte(unsigned short, unsigned char);
void __outbytestring(unsigned short, unsigned char *, unsigned long);
void __outdword(unsigned short, unsigned long);
void __outdwordstring(unsigned short, unsigned long *, unsigned long);
void __outword(unsigned short, unsigned short);
void __outwordstring(unsigned short, unsigned short *, unsigned long);
unsigned long __readcr0(void);
unsigned long __readcr2(void);
static __inline__
unsigned long __readcr3(void);
unsigned long __readcr4(void);
unsigned long __readcr8(void);
unsigned int __readdr(unsigned int);
#ifdef __i386__
static __inline__
unsigned char __readfsbyte(unsigned long);
static __inline__
unsigned __int64 __readfsqword(unsigned long);
static __inline__
unsigned short __readfsword(unsigned long);
#endif
static __inline__
unsigned __int64 __readmsr(unsigned long);
unsigned __int64 __readpmc(unsigned long);
unsigned long __segmentlimit(unsigned long);
void __sidt(void *);
static __inline__
void __stosb(unsigned char *, unsigned char, size_t);
static __inline__
void __stosd(unsigned long *, unsigned long, size_t);
static __inline__
void __stosw(unsigned short *, unsigned short, size_t);
void __svm_clgi(void);
void __svm_invlpga(void *, int);
void __svm_skinit(int);
void __svm_stgi(void);
void __svm_vmload(size_t);
void __svm_vmrun(size_t);
void __svm_vmsave(size_t);
void __ud2(void);
unsigned __int64 __ull_rshift(unsigned __int64, int);
void __vmx_off(void);
void __vmx_vmptrst(unsigned __int64 *);
void __wbinvd(void);
void __writecr0(unsigned int);
static __inline__
void __writecr3(unsigned int);
void __writecr4(unsigned int);
void __writecr8(unsigned int);
void __writedr(unsigned int, unsigned int);
void __writefsbyte(unsigned long, unsigned char);
void __writefsdword(unsigned long, unsigned long);
void __writefsqword(unsigned long, unsigned __int64);
void __writefsword(unsigned long, unsigned short);
void __writemsr(unsigned long, unsigned __int64);
static __inline__
void *_AddressOfReturnAddress(void);
static __inline__
unsigned char _BitScanForward(unsigned long *_Index, unsigned long _Mask);
static __inline__
unsigned char _BitScanReverse(unsigned long *_Index, unsigned long _Mask);
unsigned char _bittest(long const *, long);
unsigned char _bittestandcomplement(long *, long);
unsigned char _bittestandreset(long *, long);
unsigned char _bittestandset(long *, long);
void __cdecl _disable(void);
void __cdecl _enable(void);
long _InterlockedAddLargeStatistic(__int64 volatile *_Addend, long _Value);
unsigned char _interlockedbittestandreset(long volatile *, long);
unsigned char _interlockedbittestandset(long volatile *, long);
void *_InterlockedCompareExchangePointer_HLEAcquire(void *volatile *, void *,
                                                    void *);
void *_InterlockedCompareExchangePointer_HLERelease(void *volatile *, void *,
                                                    void *);
long _InterlockedExchangeAdd_HLEAcquire(long volatile *, long);
long _InterlockedExchangeAdd_HLERelease(long volatile *, long);
__int64 _InterlockedExchangeAdd64_HLEAcquire(__int64 volatile *, __int64);
__int64 _InterlockedExchangeAdd64_HLERelease(__int64 volatile *, __int64);
void __cdecl _invpcid(unsigned int, void *);
static __inline__ void
__attribute__((__deprecated__("use other intrinsics or C++11 atomics instead")))
_ReadBarrier(void);
static __inline__ void
__attribute__((__deprecated__("use other intrinsics or C++11 atomics instead")))
_ReadWriteBarrier(void);
unsigned int _rorx_u32(unsigned int, const unsigned int);
int _sarx_i32(int, unsigned int);
#if __STDC_HOSTED__
int __cdecl _setjmp(jmp_buf);
#endif
unsigned int _shlx_u32(unsigned int, unsigned int);
unsigned int _shrx_u32(unsigned int, unsigned int);
void _Store_HLERelease(long volatile *, long);
void _Store64_HLERelease(__int64 volatile *, __int64);
void _StorePointer_HLERelease(void *volatile *, void *);
static __inline__ void
__attribute__((__deprecated__("use other intrinsics or C++11 atomics instead")))
_WriteBarrier(void);
unsigned __int32 xbegin(void);
void _xend(void);
static __inline__
#define _XCR_XFEATURE_ENABLED_MASK 0
unsigned __int64 __cdecl _xgetbv(unsigned int);
void __cdecl _xsetbv(unsigned int, unsigned __int64);

/* These additional intrinsics are turned on in x64/amd64/x86_64 mode. */
#ifdef __x86_64__
void __addgsbyte(unsigned long, unsigned char);
void __addgsdword(unsigned long, unsigned long);
void __addgsqword(unsigned long, unsigned __int64);
void __addgsword(unsigned long, unsigned short);
static __inline__
void __faststorefence(void);
void __incgsbyte(unsigned long);
void __incgsdword(unsigned long);
void __incgsqword(unsigned long);
void __incgsword(unsigned long);
static __inline__
void __movsq(unsigned long long *, unsigned long long const *, size_t);
static __inline__
unsigned char __readgsbyte(unsigned long);
static __inline__
unsigned long __readgsdword(unsigned long);
static __inline__
unsigned __int64 __readgsqword(unsigned long);
unsigned short __readgsword(unsigned long);
unsigned __int64 __shiftleft128(unsigned __int64 _LowPart,
                                unsigned __int64 _HighPart,
                                unsigned char _Shift);
unsigned __int64 __shiftright128(unsigned __int64 _LowPart,
                                 unsigned __int64 _HighPart,
                                 unsigned char _Shift);
static __inline__
void __stosq(unsigned __int64 *, unsigned __int64, size_t);
unsigned char __vmx_on(unsigned __int64 *);
unsigned char __vmx_vmclear(unsigned __int64 *);
unsigned char __vmx_vmlaunch(void);
unsigned char __vmx_vmptrld(unsigned __int64 *);
unsigned char __vmx_vmread(size_t, size_t *);
unsigned char __vmx_vmresume(void);
unsigned char __vmx_vmwrite(size_t, size_t);
void __writegsbyte(unsigned long, unsigned char);
void __writegsdword(unsigned long, unsigned long);
void __writegsqword(unsigned long, unsigned __int64);
void __writegsword(unsigned long, unsigned short);
unsigned char _bittest64(__int64 const *, __int64);
unsigned char _bittestandcomplement64(__int64 *, __int64);
unsigned char _bittestandreset64(__int64 *, __int64);
unsigned char _bittestandset64(__int64 *, __int64);
long _InterlockedAnd_np(long volatile *_Value, long _Mask);
short _InterlockedAnd16_np(short volatile *_Value, short _Mask);
__int64 _InterlockedAnd64_np(__int64 volatile *_Value, __int64 _Mask);
char _InterlockedAnd8_np(char volatile *_Value, char _Mask);
unsigned char _interlockedbittestandreset64(__int64 volatile *, __int64);
unsigned char _interlockedbittestandset64(__int64 volatile *, __int64);
long _InterlockedCompareExchange_np(long volatile *_Destination, long _Exchange,
                                    long _Comparand);
unsigned char _InterlockedCompareExchange128(__int64 volatile *_Destination,
                                             __int64 _ExchangeHigh,
                                             __int64 _ExchangeLow,
                                             __int64 *_CompareandResult);
unsigned char _InterlockedCompareExchange128_np(__int64 volatile *_Destination,
                                                __int64 _ExchangeHigh,
                                                __int64 _ExchangeLow,
                                                __int64 *_ComparandResult);
short _InterlockedCompareExchange16_np(short volatile *_Destination,
                                       short _Exchange, short _Comparand);
__int64 _InterlockedCompareExchange64_np(__int64 volatile *_Destination,
                                         __int64 _Exchange, __int64 _Comparand);
void *_InterlockedCompareExchangePointer_np(void *volatile *_Destination,
                                            void *_Exchange, void *_Comparand);
long _InterlockedOr_np(long volatile *_Value, long _Mask);
short _InterlockedOr16_np(short volatile *_Value, short _Mask);
__int64 _InterlockedOr64_np(__int64 volatile *_Value, __int64 _Mask);
char _InterlockedOr8_np(char volatile *_Value, char _Mask);
long _InterlockedXor_np(long volatile *_Value, long _Mask);
short _InterlockedXor16_np(short volatile *_Value, short _Mask);
__int64 _InterlockedXor64_np(__int64 volatile *_Value, __int64 _Mask);
char _InterlockedXor8_np(char volatile *_Value, char _Mask);
unsigned __int64 _rorx_u64(unsigned __int64, const unsigned int);
__int64 _sarx_i64(__int64, unsigned int);
unsigned __int64 _shlx_u64(unsigned __int64, unsigned int);
unsigned __int64 _shrx_u64(unsigned __int64, unsigned int);
static __inline__
__int64 __mulh(__int64, __int64);
static __inline__
unsigned __int64 __umulh(unsigned __int64, unsigned __int64);
static __inline__
__int64 _mul128(__int64, __int64, __int64*);
static __inline__
unsigned __int64 _umul128(unsigned __int64,
                          unsigned __int64,
                          unsigned __int64*);

#endif /* __x86_64__ */

#if defined(__x86_64__) || defined(__arm__) || defined(__aarch64__)

static __inline__
unsigned char _BitScanForward64(unsigned long *_Index, unsigned __int64 _Mask);
static __inline__
unsigned char _BitScanReverse64(unsigned long *_Index, unsigned __int64 _Mask);

static __inline__
__int64 _InterlockedDecrement64(__int64 volatile *_Addend);
static __inline__
__int64 _InterlockedExchange64(__int64 volatile *_Target, __int64 _Value);
static __inline__
__int64 _InterlockedExchangeAdd64(__int64 volatile *_Addend, __int64 _Value);
static __inline__
__int64 _InterlockedExchangeSub64(__int64 volatile *_Subend, __int64 _Value);
static __inline__
__int64 _InterlockedIncrement64(__int64 volatile *_Addend);
static __inline__
__int64 _InterlockedOr64(__int64 volatile *_Value, __int64 _Mask);
static __inline__
__int64 _InterlockedXor64(__int64 volatile *_Value, __int64 _Mask);
static __inline__
__int64 _InterlockedAnd64(__int64 volatile *_Value, __int64 _Mask);

#endif

/*----------------------------------------------------------------------------*\
|* Interlocked Exchange Add
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
char _InterlockedExchangeAdd8_acq(char volatile *_Addend, char _Value);
char _InterlockedExchangeAdd8_nf(char volatile *_Addend, char _Value);
char _InterlockedExchangeAdd8_rel(char volatile *_Addend, char _Value);
short _InterlockedExchangeAdd16_acq(short volatile *_Addend, short _Value);
short _InterlockedExchangeAdd16_nf(short volatile *_Addend, short _Value);
short _InterlockedExchangeAdd16_rel(short volatile *_Addend, short _Value);
long _InterlockedExchangeAdd_acq(long volatile *_Addend, long _Value);
long _InterlockedExchangeAdd_nf(long volatile *_Addend, long _Value);
long _InterlockedExchangeAdd_rel(long volatile *_Addend, long _Value);
__int64 _InterlockedExchangeAdd64_acq(__int64 volatile *_Addend, __int64 _Value);
__int64 _InterlockedExchangeAdd64_nf(__int64 volatile *_Addend, __int64 _Value);
__int64 _InterlockedExchangeAdd64_rel(__int64 volatile *_Addend, __int64 _Value);
#endif
/*----------------------------------------------------------------------------*\
|* Interlocked Increment
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
short _InterlockedIncrement16_acq(short volatile *_Value);
short _InterlockedIncrement16_nf(short volatile *_Value);
short _InterlockedIncrement16_rel(short volatile *_Value);
long _InterlockedIncrement_acq(long volatile *_Value);
long _InterlockedIncrement_nf(long volatile *_Value);
long _InterlockedIncrement_rel(long volatile *_Value);
__int64 _InterlockedIncrement64_acq(__int64 volatile *_Value);
__int64 _InterlockedIncrement64_nf(__int64 volatile *_Value);
__int64 _InterlockedIncrement64_rel(__int64 volatile *_Value);
#endif
/*----------------------------------------------------------------------------*\
|* Interlocked Decrement
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
short _InterlockedDecrement16_acq(short volatile *_Value);
short _InterlockedDecrement16_nf(short volatile *_Value);
short _InterlockedDecrement16_rel(short volatile *_Value);
long _InterlockedDecrement_acq(long volatile *_Value);
long _InterlockedDecrement_nf(long volatile *_Value);
long _InterlockedDecrement_rel(long volatile *_Value);
__int64 _InterlockedDecrement64_acq(__int64 volatile *_Value);
__int64 _InterlockedDecrement64_nf(__int64 volatile *_Value);
__int64 _InterlockedDecrement64_rel(__int64 volatile *_Value);
#endif
/*----------------------------------------------------------------------------*\
|* Interlocked And
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
char _InterlockedAnd8_acq(char volatile *_Value, char _Mask);
char _InterlockedAnd8_nf(char volatile *_Value, char _Mask);
char _InterlockedAnd8_rel(char volatile *_Value, char _Mask);
short _InterlockedAnd16_acq(short volatile *_Value, short _Mask);
short _InterlockedAnd16_nf(short volatile *_Value, short _Mask);
short _InterlockedAnd16_rel(short volatile *_Value, short _Mask);
long _InterlockedAnd_acq(long volatile *_Value, long _Mask);
long _InterlockedAnd_nf(long volatile *_Value, long _Mask);
long _InterlockedAnd_rel(long volatile *_Value, long _Mask);
__int64 _InterlockedAnd64_acq(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedAnd64_nf(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedAnd64_rel(__int64 volatile *_Value, __int64 _Mask);
#endif
/*----------------------------------------------------------------------------*\
|* Bit Counting and Testing
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
unsigned char _interlockedbittestandset_acq(long volatile *_BitBase,
                                            long _BitPos);
unsigned char _interlockedbittestandset_nf(long volatile *_BitBase,
                                           long _BitPos);
unsigned char _interlockedbittestandset_rel(long volatile *_BitBase,
                                            long _BitPos);
unsigned char _interlockedbittestandreset_acq(long volatile *_BitBase,
                                              long _BitPos);
unsigned char _interlockedbittestandreset_nf(long volatile *_BitBase,
                                             long _BitPos);
unsigned char _interlockedbittestandreset_rel(long volatile *_BitBase,
                                              long _BitPos);
#endif
/*----------------------------------------------------------------------------*\
|* Interlocked Or
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
char _InterlockedOr8_acq(char volatile *_Value, char _Mask);
char _InterlockedOr8_nf(char volatile *_Value, char _Mask);
char _InterlockedOr8_rel(char volatile *_Value, char _Mask);
short _InterlockedOr16_acq(short volatile *_Value, short _Mask);
short _InterlockedOr16_nf(short volatile *_Value, short _Mask);
short _InterlockedOr16_rel(short volatile *_Value, short _Mask);
long _InterlockedOr_acq(long volatile *_Value, long _Mask);
long _InterlockedOr_nf(long volatile *_Value, long _Mask);
long _InterlockedOr_rel(long volatile *_Value, long _Mask);
__int64 _InterlockedOr64_acq(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedOr64_nf(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedOr64_rel(__int64 volatile *_Value, __int64 _Mask);
#endif
/*----------------------------------------------------------------------------*\
|* Interlocked Xor
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
char _InterlockedXor8_acq(char volatile *_Value, char _Mask);
char _InterlockedXor8_nf(char volatile *_Value, char _Mask);
char _InterlockedXor8_rel(char volatile *_Value, char _Mask);
short _InterlockedXor16_acq(short volatile *_Value, short _Mask);
short _InterlockedXor16_nf(short volatile *_Value, short _Mask);
short _InterlockedXor16_rel(short volatile *_Value, short _Mask);
long _InterlockedXor_acq(long volatile *_Value, long _Mask);
long _InterlockedXor_nf(long volatile *_Value, long _Mask);
long _InterlockedXor_rel(long volatile *_Value, long _Mask);
__int64 _InterlockedXor64_acq(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedXor64_nf(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedXor64_rel(__int64 volatile *_Value, __int64 _Mask);
#endif
/*----------------------------------------------------------------------------*\
|* Interlocked Exchange
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
char _InterlockedExchange8_acq(char volatile *_Target, char _Value);
char _InterlockedExchange8_nf(char volatile *_Target, char _Value);
char _InterlockedExchange8_rel(char volatile *_Target, char _Value);
short _InterlockedExchange16_acq(short volatile *_Target, short _Value);
short _InterlockedExchange16_nf(short volatile *_Target, short _Value);
short _InterlockedExchange16_rel(short volatile *_Target, short _Value);
long _InterlockedExchange_acq(long volatile *_Target, long _Value);
long _InterlockedExchange_nf(long volatile *_Target, long _Value);
long _InterlockedExchange_rel(long volatile *_Target, long _Value);
__int64 _InterlockedExchange64_acq(__int64 volatile *_Target, __int64 _Value);
__int64 _InterlockedExchange64_nf(__int64 volatile *_Target, __int64 _Value);
__int64 _InterlockedExchange64_rel(__int64 volatile *_Target, __int64 _Value);
#endif
/*----------------------------------------------------------------------------*\
|* Interlocked Compare Exchange
\*----------------------------------------------------------------------------*/
#if defined(__arm__) || defined(__aarch64__)
char _InterlockedCompareExchange8_acq(char volatile *_Destination,
                             char _Exchange, char _Comparand);
char _InterlockedCompareExchange8_nf(char volatile *_Destination,
                             char _Exchange, char _Comparand);
char _InterlockedCompareExchange8_rel(char volatile *_Destination,
                             char _Exchange, char _Comparand);
short _InterlockedCompareExchange16_acq(short volatile *_Destination,
                              short _Exchange, short _Comparand);
short _InterlockedCompareExchange16_nf(short volatile *_Destination,
                              short _Exchange, short _Comparand);
short _InterlockedCompareExchange16_rel(short volatile *_Destination,
                              short _Exchange, short _Comparand);
long _InterlockedCompareExchange_acq(long volatile *_Destination,
                              long _Exchange, long _Comparand);
long _InterlockedCompareExchange_nf(long volatile *_Destination,
                              long _Exchange, long _Comparand);
long _InterlockedCompareExchange_rel(long volatile *_Destination,
                              long _Exchange, long _Comparand);
__int64 _InterlockedCompareExchange64_acq(__int64 volatile *_Destination,
                              __int64 _Exchange, __int64 _Comparand);
__int64 _InterlockedCompareExchange64_nf(__int64 volatile *_Destination,
                              __int64 _Exchange, __int64 _Comparand);
__int64 _InterlockedCompareExchange64_rel(__int64 volatile *_Destination,
                              __int64 _Exchange, __int64 _Comparand);
#endif

/*----------------------------------------------------------------------------*\
|* movs, stos
\*----------------------------------------------------------------------------*/
#if defined(__i386__) || defined(__x86_64__)
static __inline__ void __DEFAULT_FN_ATTRS
__movsb(unsigned char *__dst, unsigned char const *__src, size_t __n) {
  __asm__ __volatile__("rep movsb" : "+D"(__dst), "+S"(__src), "+c"(__n)
                       : : "memory");
}
static __inline__ void __DEFAULT_FN_ATTRS
__movsd(unsigned long *__dst, unsigned long const *__src, size_t __n) {
  __asm__ __volatile__("rep movsl" : "+D"(__dst), "+S"(__src), "+c"(__n)
                       : : "memory");
}
static __inline__ void __DEFAULT_FN_ATTRS
__movsw(unsigned short *__dst, unsigned short const *__src, size_t __n) {
  __asm__ __volatile__("rep movsw" : "+D"(__dst), "+S"(__src), "+c"(__n)
                       : : "memory");
}
static __inline__ void __DEFAULT_FN_ATTRS
__stosd(unsigned long *__dst, unsigned long __x, size_t __n) {
  __asm__ __volatile__("rep stosl" : "+D"(__dst), "+c"(__n) : "a"(__x)
                       : "memory");
}
static __inline__ void __DEFAULT_FN_ATTRS
__stosw(unsigned short *__dst, unsigned short __x, size_t __n) {
  __asm__ __volatile__("rep stosw" : "+D"(__dst), "+c"(__n) : "a"(__x)
                       : "memory");
}
#endif
#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS
__movsq(unsigned long long *__dst, unsigned long long const *__src, size_t __n) {
  __asm__ __volatile__("rep movsq" : "+D"(__dst), "+S"(__src), "+c"(__n)
                       : : "memory");
}
static __inline__ void __DEFAULT_FN_ATTRS
__stosq(unsigned __int64 *__dst, unsigned __int64 __x, size_t __n) {
  __asm__ __volatile__("rep stosq" : "+D"(__dst), "+c"(__n) : "a"(__x)
                       : "memory");
}
#endif

/*----------------------------------------------------------------------------*\
|* Misc
\*----------------------------------------------------------------------------*/
#if defined(__i386__) || defined(__x86_64__)
static __inline__ void __DEFAULT_FN_ATTRS
__cpuid(int __info[4], int __level) {
  __asm__ ("cpuid" : "=a"(__info[0]), "=b" (__info[1]), "=c"(__info[2]), "=d"(__info[3])
                   : "a"(__level), "c"(0));
}
static __inline__ void __DEFAULT_FN_ATTRS
__cpuidex(int __info[4], int __level, int __ecx) {
  __asm__ ("cpuid" : "=a"(__info[0]), "=b" (__info[1]), "=c"(__info[2]), "=d"(__info[3])
                   : "a"(__level), "c"(__ecx));
}
static __inline__ unsigned __int64 __cdecl __DEFAULT_FN_ATTRS
_xgetbv(unsigned int __xcr_no) {
  unsigned int __eax, __edx;
  __asm__ ("xgetbv" : "=a" (__eax), "=d" (__edx) : "c" (__xcr_no));
  return ((unsigned __int64)__edx << 32) | __eax;
}
static __inline__ void __DEFAULT_FN_ATTRS
__halt(void) {
  __asm__ volatile ("hlt");
}
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
static __inline__ void __DEFAULT_FN_ATTRS
__nop(void) {
  __asm__ volatile ("nop");
}
#endif

/*----------------------------------------------------------------------------*\
|* MS AArch64 specific
\*----------------------------------------------------------------------------*/
#if defined(__aarch64__)
unsigned __int64 __getReg(int);
long _InterlockedAdd(long volatile *Addend, long Value);
__int64 _ReadStatusReg(int);
void _WriteStatusReg(int, __int64);

static inline unsigned short _byteswap_ushort (unsigned short val) {
  return __builtin_bswap16(val);
}
static inline unsigned long _byteswap_ulong (unsigned long val) {
  return __builtin_bswap32(val);
}
static inline unsigned __int64 _byteswap_uint64 (unsigned __int64 val) {
  return __builtin_bswap64(val);
}
#endif

/*----------------------------------------------------------------------------*\
|* Privileged intrinsics
\*----------------------------------------------------------------------------*/
#if defined(__i386__) || defined(__x86_64__)
static __inline__ unsigned __int64 __DEFAULT_FN_ATTRS
__readmsr(unsigned long __register) {
  // Loads the contents of a 64-bit model specific register (MSR) specified in
  // the ECX register into registers EDX:EAX. The EDX register is loaded with
  // the high-order 32 bits of the MSR and the EAX register is loaded with the
  // low-order 32 bits. If less than 64 bits are implemented in the MSR being
  // read, the values returned to EDX:EAX in unimplemented bit locations are
  // undefined.
  unsigned long __edx;
  unsigned long __eax;
  __asm__ ("rdmsr" : "=d"(__edx), "=a"(__eax) : "c"(__register));
  return (((unsigned __int64)__edx) << 32) | (unsigned __int64)__eax;
}

static __inline__ unsigned long __DEFAULT_FN_ATTRS
__readcr3(void) {
  unsigned long __cr3_val;
  __asm__ __volatile__ ("mov %%cr3, %0" : "=q"(__cr3_val) : : "memory");
  return __cr3_val;
}

static __inline__ void __DEFAULT_FN_ATTRS
__writecr3(unsigned int __cr3_val) {
  __asm__ ("mov %0, %%cr3" : : "q"(__cr3_val) : "memory");
}
#endif

#ifdef __cplusplus
}
#endif

#undef __DEFAULT_FN_ATTRS

#endif /* __INTRIN_H */
#endif /* _MSC_VER */
