/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet

BEGIN
{
	@["j-church"] = quantize(1, 100);
	@["j-church"] = quantize(1, -99);
	@["j-church"] = quantize(1, -1);
	val = 123;
}

BEGIN
{
	@["k-ingleside"] = quantize(1, -val);
}

BEGIN
{
	@["l-taraval"] = quantize(0, -val);
	@["l-taraval"] = quantize(-1, -val);
	@["l-taraval"] = quantize(1, val);
	@["l-taraval"] = quantize(1, val);
}

BEGIN
{
	@["m-oceanview"] = quantize(1, (1 << 63) - 1);
	@["m-oceanview"] = quantize(1);
	@["m-oceanview"] = quantize(2, (1 << 63) - 1);
	@["m-oceanview"] = quantize(8, 400000);
}

BEGIN
{
	@["n-judah"] = quantize(1, val);
	@["n-judah"] = quantize(2, val);
	@["n-judah"] = quantize(2, val);
	@["n-judah"] = quantize(2);
}

BEGIN
{
	this->i = 1;
	this->val = (1 << 63) - 1;

	@["f-market"] = quantize(this->i, this->val);
	this->i <<= 1;
	this->val >>= 1;

	@["f-market"] = quantize(this->i, this->val);
	this->i <<= 1;
	this->val >>= 1;

	@["f-market"] = quantize(this->i, this->val);
	this->i <<= 1;
	this->val >>= 1;

	@["f-market"] = quantize(this->i, this->val);
	this->i <<= 1;
	this->val >>= 1;
}

BEGIN
{
	this->i = 1;
	this->val = (1 << 63) - 4;

	@["s-castro"] = quantize(this->i, this->val);
	this->i <<= 1;
	this->val >>= 1;

	@["s-castro"] = quantize(this->i, this->val);
	this->i <<= 1;
	this->val >>= 1;

	@["s-castro"] = quantize(this->i, this->val);
	this->i <<= 1;
	this->val >>= 1;

	@["s-castro"] = quantize(this->i, this->val);
	this->i <<= 1;
	this->val >>= 1;
}

BEGIN
{
	exit(0);
}
