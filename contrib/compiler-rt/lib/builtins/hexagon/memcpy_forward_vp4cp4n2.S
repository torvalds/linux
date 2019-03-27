//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// An optimized version of a memcpy which is equivalent to the following loop:
//
//   volatile unsigned *dest;
//   unsigned *src;
//
//   for (i = 0; i < num_words; ++i)
//     *dest++ = *src++;
//
// The corresponding C prototype for this function would be
// void hexagon_memcpy_forward_vp4cp4n2(volatile unsigned *dest,
//                                      const unsigned *src,
//                                      unsigned num_words);
//
// *** Both dest and src must be aligned to 32-bit boundaries. ***
// The code does not perform any runtime checks for this, and will fail
// in bad ways if this requirement is not met.
//
// The "forward" in the name refers to the fact that the function copies
// the words going forward in memory.  It is incorrect to use this function
// for cases where the original code copied words in any other order.
//
// *** This function is only for the use by the compiler. ***
// The only indended use is for the LLVM compiler to generate calls to
// this function, when a mem-copy loop, like the one above, is detected.

  .text

// Inputs:
//   r0: dest
//   r1: src
//   r2: num_words

  .globl  hexagon_memcpy_forward_vp4cp4n2
  .balign 32
  .type   hexagon_memcpy_forward_vp4cp4n2,@function
hexagon_memcpy_forward_vp4cp4n2:

    // Compute r3 to be the number of words remaining in the current page.
    // At the same time, compute r4 to be the number of 32-byte blocks
    // remaining in the page (for prefetch).
  {
    r3 = sub(##4096, r1)
    r5 = lsr(r2, #3)
  }
  {
    // The word count before end-of-page is in the 12 lowest bits of r3.
    // (If the address in r1 was already page-aligned, the bits are 0.)
    r3 = extractu(r3, #10, #2)
    r4 = extractu(r3, #7, #5)
  }
  {
    r3 = minu(r2, r3)
    r4 = minu(r5, r4)
  }
  {
    r4 = or(r4, ##2105344)      // 2105344 = 0x202000
    p0 = cmp.eq(r3, #0)
    if (p0.new) jump:nt .Lskipprolog
  }
    l2fetch(r1, r4)
  {
    loop0(.Lprolog, r3)
    r2 = sub(r2, r3)            // r2 = number of words left after the prolog.
  }
  .falign
.Lprolog:
  {
    r4 = memw(r1++#4)
    memw(r0++#4) = r4.new
  } :endloop0
.Lskipprolog:
  {
    // Let r3 = number of whole pages left (page = 1024 words).
    r3 = lsr(r2, #10)
    if (cmp.eq(r3.new, #0)) jump:nt .Lskipmain
  }
  {
    loop1(.Lout, r3)
    r2 = extractu(r2, #10, #0)  // r2 = r2 & 1023
    r3 = ##2105472              // r3 = 0x202080 (prefetch info)
  }
    // Iterate over pages.
  .falign
.Lout:
    // Prefetch each individual page.
    l2fetch(r1, r3)
    loop0(.Lpage, #512)
  .falign
.Lpage:
    r5:4 = memd(r1++#8)
  {
    memw(r0++#8) = r4
    memw(r0+#4) = r5
  } :endloop0:endloop1
.Lskipmain:
  {
    r3 = ##2105344              // r3 = 0x202000 (prefetch info)
    r4 = lsr(r2, #3)            // r4 = number of 32-byte blocks remaining.
    p0 = cmp.eq(r2, #0)
    if (p0.new) jumpr:nt r31
  }
  {
    r3 = or(r3, r4)
    loop0(.Lepilog, r2)
  }
    l2fetch(r1, r3)
  .falign
.Lepilog:
  {
    r4 = memw(r1++#4)
    memw(r0++#4) = r4.new
  } :endloop0

    jumpr r31

.size hexagon_memcpy_forward_vp4cp4n2, . - hexagon_memcpy_forward_vp4cp4n2
