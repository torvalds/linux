/*
 * Copyright (C) Paul Mackerras 1997.
 * Copyright (C) Leigh Brown 2002.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "of1275.h"

int
ofstdio(ihandle *stdin, ihandle *stdout, ihandle *stderr)
{
    ihandle in, out;
    phandle chosen;

    if ((chosen = finddevice("/chosen")) == OF_INVALID_HANDLE)
	goto err;
    if (getprop(chosen, "stdout", &out, sizeof(out)) != 4)
	goto err;
    if (getprop(chosen, "stdin", &in, sizeof(in)) != 4)
	goto err;

    *stdin  = in;
    *stdout = out;
    *stderr = out;
    return 0;
err:
    return -1;
}
