#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Borrowed from gcc: gcc/testsuite/gcc.target/s390/nobp-section-type-conflict.c
# Checks that we don't get error: section type conflict with ‘put_page’.

cat << "END" | $@ -x c - -fno-PIE -march=z10 -mindirect-branch=thunk-extern -mfunction-return=thunk-extern -mindirect-branch-table -O2 -c -o /dev/null
int a;
int b (void);
void c (int);

static void
put_page (void)
{
  if (b ())
    c (a);
}

__attribute__ ((__section__ (".init.text"), __cold__)) void
d (void)
{
  put_page ();
  put_page ();
}
END
