/*
 * divsufsort@W64BIT@.h for libdivsufsort@W64BIT@
 * Copyright (c) 2003-2008 Yuta Mori All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DIVSUFSORT@W64BIT@_H
#define _DIVSUFSORT@W64BIT@_H 1

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

@INCFILE@

#ifndef DIVSUFSORT_API
# ifdef DIVSUFSORT_BUILD_DLL
#  define DIVSUFSORT_API @DIVSUFSORT_EXPORT@
# else
#  define DIVSUFSORT_API @DIVSUFSORT_IMPORT@
# endif
#endif

/*- Datatypes -*/
#ifndef SAUCHAR_T
#define SAUCHAR_T
typedef @SAUCHAR_TYPE@ sauchar_t;
#endif /* SAUCHAR_T */
#ifndef SAINT_T
#define SAINT_T
typedef @SAINT32_TYPE@ saint_t;
#endif /* SAINT_T */
#ifndef SAIDX@W64BIT@_T
#define SAIDX@W64BIT@_T
typedef @SAINDEX_TYPE@ saidx@W64BIT@_t;
#endif /* SAIDX@W64BIT@_T */
#ifndef PRIdSAINT_T
#define PRIdSAINT_T @SAINT_PRId@
#endif /* PRIdSAINT_T */
#ifndef PRIdSAIDX@W64BIT@_T
#define PRIdSAIDX@W64BIT@_T @SAINDEX_PRId@
#endif /* PRIdSAIDX@W64BIT@_T */


/*- Prototypes -*/

/**
 * Constructs the suffix array of a given string.
 * @param T[0..n-1] The input string.
 * @param SA[0..n-1] The output array of suffixes.
 * @param n The length of the given string.
 * @return 0 if no error occurred, -1 or -2 otherwise.
 */
DIVSUFSORT_API
saint_t
divsufsort@W64BIT@(const sauchar_t *T, saidx@W64BIT@_t *SA, saidx@W64BIT@_t n);

/**
 * Constructs the burrows-wheeler transformed string of a given string.
 * @param T[0..n-1] The input string.
 * @param U[0..n-1] The output string. (can be T)
 * @param A[0..n-1] The temporary array. (can be NULL)
 * @param n The length of the given string.
 * @return The primary index if no error occurred, -1 or -2 otherwise.
 */
DIVSUFSORT_API
saidx@W64BIT@_t
divbwt@W64BIT@(const sauchar_t *T, sauchar_t *U, saidx@W64BIT@_t *A, saidx@W64BIT@_t n);

/**
 * Returns the version of the divsufsort library.
 * @return The version number string.
 */
DIVSUFSORT_API
const char *
divsufsort@W64BIT@_version(void);


/**
 * Constructs the burrows-wheeler transformed string of a given string and suffix array.
 * @param T[0..n-1] The input string.
 * @param U[0..n-1] The output string. (can be T)
 * @param SA[0..n-1] The suffix array. (can be NULL)
 * @param n The length of the given string.
 * @param idx The output primary index.
 * @return 0 if no error occurred, -1 or -2 otherwise.
 */
DIVSUFSORT_API
saint_t
bw_transform@W64BIT@(const sauchar_t *T, sauchar_t *U,
             saidx@W64BIT@_t *SA /* can NULL */,
             saidx@W64BIT@_t n, saidx@W64BIT@_t *idx);

/**
 * Inverse BW-transforms a given BWTed string.
 * @param T[0..n-1] The input string.
 * @param U[0..n-1] The output string. (can be T)
 * @param A[0..n-1] The temporary array. (can be NULL)
 * @param n The length of the given string.
 * @param idx The primary index.
 * @return 0 if no error occurred, -1 or -2 otherwise.
 */
DIVSUFSORT_API
saint_t
inverse_bw_transform@W64BIT@(const sauchar_t *T, sauchar_t *U,
                     saidx@W64BIT@_t *A /* can NULL */,
                     saidx@W64BIT@_t n, saidx@W64BIT@_t idx);

/**
 * Checks the correctness of a given suffix array.
 * @param T[0..n-1] The input string.
 * @param SA[0..n-1] The input suffix array.
 * @param n The length of the given string.
 * @param verbose The verbose mode.
 * @return 0 if no error occurred.
 */
DIVSUFSORT_API
saint_t
sufcheck@W64BIT@(const sauchar_t *T, const saidx@W64BIT@_t *SA, saidx@W64BIT@_t n, saint_t verbose);

/**
 * Search for the pattern P in the string T.
 * @param T[0..Tsize-1] The input string.
 * @param Tsize The length of the given string.
 * @param P[0..Psize-1] The input pattern string.
 * @param Psize The length of the given pattern string.
 * @param SA[0..SAsize-1] The input suffix array.
 * @param SAsize The length of the given suffix array.
 * @param idx The output index.
 * @return The count of matches if no error occurred, -1 otherwise.
 */
DIVSUFSORT_API
saidx@W64BIT@_t
sa_search@W64BIT@(const sauchar_t *T, saidx@W64BIT@_t Tsize,
          const sauchar_t *P, saidx@W64BIT@_t Psize,
          const saidx@W64BIT@_t *SA, saidx@W64BIT@_t SAsize,
          saidx@W64BIT@_t *left);

/**
 * Search for the character c in the string T.
 * @param T[0..Tsize-1] The input string.
 * @param Tsize The length of the given string.
 * @param SA[0..SAsize-1] The input suffix array.
 * @param SAsize The length of the given suffix array.
 * @param c The input character.
 * @param idx The output index.
 * @return The count of matches if no error occurred, -1 otherwise.
 */
DIVSUFSORT_API
saidx@W64BIT@_t
sa_simplesearch@W64BIT@(const sauchar_t *T, saidx@W64BIT@_t Tsize,
                const saidx@W64BIT@_t *SA, saidx@W64BIT@_t SAsize,
                saint_t c, saidx@W64BIT@_t *left);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* _DIVSUFSORT@W64BIT@_H */
