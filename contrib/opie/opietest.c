/* opietest.c: Quick, though definitely not complete, regression test for
               libopie. This is intended to catch two things:

	(1) when changes break something
        (2) if some system wierdness (libc, compiler, or CPU/hardware) is
            not getting along at all with OPIE.

        It's safe to say that, if tests fail, OPIE isn't going to work right
on your system. The converse is not such a safe statement.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.4. Use struct opie_key for key blocks.
	Modified by cmetz for OPIE 2.31. Added a couple of new checks,
		removed a few commented-out checks for functions that
		no longer exist, added test-skip capability.
	Modified by cmetz for OPIE 2.3. Use new calling conventions for
		opiebtoa8()/atob8(). opiegenerator() outputs hex now.
	Modified by cmetz for OPIE 2.22. Test opielock()/opieunlock()
		refcount support.
	Created by cmetz for OPIE 2.2.
*/
#include "opie_cfg.h"
#include <stdio.h>
#include "opie.h"

char buffer[1024];

int testatob8()
{
  static char testin[] = "0123456789abcdef";
  static unsigned char testout[sizeof(struct opie_otpkey)] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
  struct opie_otpkey key;

  if (!opieatob8(&key, testin))
    return -1;

  if (memcmp(&key, testout, sizeof(testout)))
    return -1;
  
  return 0;
}

int testbtoa8()
{
  static unsigned char testin[sizeof(struct opie_otpkey)] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
  static char testout[] = "0123456789abcdef";
  struct opie_otpkey testin_aligned;

  memcpy(&testin_aligned, testin, sizeof(struct opie_otpkey));
    
  if (!opiebtoa8(buffer, &testin_aligned))
    return -1;

  if (memcmp(buffer, testout, sizeof(testout)))
    return -1;
  
  return 0;  
}

int testbtoe()
{
  static unsigned char testin[sizeof(struct opie_otpkey)] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
  static char testout[] = "AIM HEW BLUM FED MITE WARM";
  struct opie_otpkey testin_aligned;
  
  memcpy(&testin_aligned, testin, sizeof(struct opie_otpkey));

  if (!opiebtoe(buffer, &testin_aligned))
    return -1;

  if (memcmp(buffer, testout, sizeof(testout)))
    return -1;
  
  return 0;  
}

int testetob()
{
  static char testin[] = "AIM HEW BLUM FED MITE WARM";
  static unsigned char testout[sizeof(struct opie_otpkey)] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
  struct opie_otpkey key;

  if (opieetob(&key, testin) != 1)
    return -1;

  if (memcmp(&key, testout, sizeof(testout)))
    return -1;
  
  return 0;  
}

int testgenerator()
{
  static char testin1[] = "otp-md5 123 ke1234";
  static char testin2[] = "this is a test";
  /*  static char testout[] = "END KERN BALM NICK EROS WAVY"; */
  static char testout[] = "11D4 C147 E227 C1F1";

  if (opiegenerator(testin1, testin2, buffer))
    return -1;

  if (memcmp(buffer, testout, sizeof(testout)))
    return -1;
  
  return 0;  
}

int testgetsequence()
{
  struct opie testin;
  testin.opie_n = 42;

  if (opiegetsequence(&testin) != 42)
    return -1;

  return 0;
}

int testhashmd4()
{
  static unsigned char testin[sizeof(struct opie_otpkey)] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
  static unsigned char testout[sizeof(struct opie_otpkey)] = { 0x9f, 0x40, 0xfb, 0x84, 0xb, 0xf8, 0x7f, 0x4b };
  struct opie_otpkey testin_aligned;

  memcpy(&testin_aligned, testin, sizeof(struct opie_otpkey));

  opiehash(&testin_aligned, 4);

  if (memcmp(&testin_aligned, testout, sizeof(struct opie_otpkey)))
    return -1;

  return 0;
}

int testhashmd5()
{
  static unsigned char testin[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
  static unsigned char testout[] = { 0x78, 0xdd, 0x1a, 0x37, 0xf8, 0x91, 0x54, 0xe1 };
  struct opie_otpkey testin_aligned;

  memcpy(&testin_aligned, testin, sizeof(struct opie_otpkey));

  opiehash(&testin_aligned, 5);

  if (memcmp(&testin_aligned, testout, sizeof(struct opie_otpkey)))
    return -1;

  return 0;
}

int testinsecure()
{
  opieinsecure();

  return 0;
}

int testkeycrunch()
{
  static char testin1[] = "ke1234";
  static char testin2[] = "this is a test";
  static unsigned char testout[sizeof(struct opie_otpkey)] = { 0x2e, 0xd3, 0x5d, 0x74, 0x3e, 0xa9, 0xe9, 0xe8 };
  struct opie_otpkey opie_otpkey;

  if (opiekeycrunch(5, &opie_otpkey, testin1, testin2))
    return -1;

  if (memcmp(&opie_otpkey, testout, sizeof(struct opie_otpkey)))
    return -1;

  return 0;
}

int testlock()
{
  int i;

  if (getuid())
    return -2;

  for (i = 0; i < 3; i++)
    if (opielock("__opietest"))
      return -1;

  return 0;
}

int testpasscheck()
{
  static char testin1[] = "abadone";
  static char testin2[] = "A more reasonable choice.";

  if (!opiepasscheck(testin1))
    return -1;

  if (opiepasscheck(testin2))
    return -1;

  return 0;
}

int testrandomchallenge()
{
  char buffer[OPIE_CHALLENGE_MAX+1];

  opierandomchallenge(buffer);

  if (strncmp(buffer, "otp-", 4))
    return -1;

  return 0;
}

int testunlock()
{
  int i;

  if (getuid())
    return -2;

  for (i = 0; i < 3; i++)
    if (opieunlock())
      return -1;

  if (opieunlock() != -1)
    return -1;

  return 0;
}

struct opietest {
  int (*f)();
  char *n;
};

static struct opietest opietests[] = {
  { testatob8, "atob8" },
  { testbtoa8, "btoa8" },
  { testbtoe, "btoe" },
  { testetob, "etob" },
/*  { testchallenge, "challenge" }, */
  { testgenerator, "generator" },
  { testgetsequence, "getsequence" },
  { testhashmd4, "hash(MD4)" },
  { testhashmd5, "hash(MD5)" },
  { testinsecure, "insecure" },
  { testkeycrunch, "keycrunch" },
  { testlock, "lock" },
  { testrandomchallenge, "randomchallenge" },
/* { testreadpass, "readpass" }, */
  { testunlock, "unlock" },
/*  { testverify, "verify" }, */
  { NULL, NULL }
};

int main FUNCTION((argc, argv), int argc AND char *argv[])
{
  struct opietest *opietest;
  int tests_passed = 0;
  int tests_failed = 0;
  int tests_skipped = 0;
  int ntests = 0, testn = 0;

  if (getuid() != geteuid()) {
    fprintf(stderr, "opietest: do not make this program setuid!\n");
    exit(1);
  };

  for (opietest = opietests; opietest->n; opietest++)
    ntests++;

  printf("opietest: executing %d tests\n", ntests);

  for (opietest = opietests, testn = 1; opietest->n; opietest++) {
    printf("(%2d/%2d) testing opie%s... ", testn++, ntests, opietest->n);
    switch(opietest->f()) {
      case -2:
        printf("skipped\n");
        tests_skipped++;
        opietest->f = NULL;
        break;
      case -1:
        printf("FAILED!\n");
        tests_failed++;
        break;
      case 0:
        printf("passed\n");
        tests_passed++;
        opietest->f = NULL;
        break;
    }
  }

  printf("opietest: completed %d tests. %d tests passed, %d tests skipped, %d tests failed.\n", ntests, tests_passed, tests_skipped, tests_failed);
  if (tests_failed) {
    printf("opietest: please correct the following failures before attempting to use OPIE:\n");
    for (opietest = opietests; opietest->n; opietest++)
      if (opietest->f)
	printf("          opie%s\n", opietest->n);
    exit(1);
  }
  exit(0);
}
