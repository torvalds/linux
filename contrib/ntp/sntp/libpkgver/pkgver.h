/*
 * 
 *   Copyright 2015 Harlan Stenn.  Used by NTP with permission.
 *
 *   Author: Harlan Stenn <harlan@pfcs.com>
 *
 *   Copying and distribution of this file, with or without modification, are
 *   permitted in any medium without royalty provided the copyright notice
 *   and this notice are preserved. This file is offered as-is, without any
 *   warranty.
 */

extern int colcomp(char *s1, char *s2);

#define PKG_VER_LT(x)	(colcomp((x), PACKAGE_VERSION) < 0)
#define PKG_VER_LE(x)	(colcomp((x), PACKAGE_VERSION) <= 0)
#define PKG_VER_EQ(x)	(colcomp((x), PACKAGE_VERSION) == 0)
#define PKG_VER_GE(x)	(colcomp((x), PACKAGE_VERSION) >= 0)
#define PKG_VER_GT(x)	(colcomp((x), PACKAGE_VERSION) > 0)
