/* hashlen.c: The opiehashlen() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.4. Use struct opie_otpkey, isolate variables.
	Created by cmetz for OPIE 2.3.

$FreeBSD$
*/

#include <sys/endian.h>

#include "opie_cfg.h"
#include "opie.h"

#include <sha.h>
#include <md4.h>
#include <md5.h>

VOIDRET opiehashlen FUNCTION((algorithm, in, out, n), int algorithm AND
VOIDPTR in AND struct opie_otpkey *out AND int n)
{
  UINT4 *results = (UINT4 *)out;
  UINT4 mdx_tmp[4];

  switch(algorithm) {
    case 3: {
      SHA_CTX sha;
      UINT4 digest[5];
      SHA1_Init(&sha);
      SHA1_Update(&sha, (unsigned char *)in, n);
      SHA1_Final((unsigned char *)digest, &sha);
      results[0] = digest[0] ^ digest[2] ^ digest[4];
      results[1] = digest[1] ^ digest[3];

      /*
       * RFC2289 mandates that we convert SHA1 digest from big-endian to little
       * see Appendix A.
       */
      results[0] = bswap32(results[0]);
      results[1] = bswap32(results[1]);
      break;
    }
    case 4: {
      MD4_CTX mdx;
      MD4Init(&mdx);
      MD4Update(&mdx, (unsigned char *)in, n);
      MD4Final((unsigned char *)mdx_tmp, &mdx);
      results[0] = mdx_tmp[0] ^ mdx_tmp[2];
      results[1] = mdx_tmp[1] ^ mdx_tmp[3];
      break;
    }
    case 5: {
      MD5_CTX mdx;
      MD5Init(&mdx);
      MD5Update(&mdx, (unsigned char *)in, n);
      MD5Final((unsigned char *)mdx_tmp, &mdx);
      results[0] = mdx_tmp[0] ^ mdx_tmp[2];
      results[1] = mdx_tmp[1] ^ mdx_tmp[3];
      break;
    }
  }
}
