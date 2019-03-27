/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

using System;

class TPointerExpr : TPointerBase {

	string expr;
	int min, max;

	internal TPointerExpr(string expr, int min, int max)
	{
		this.expr = expr;
		this.min = min;
		this.max = max;
	}

	internal override bool ToBool(TValue vp)
	{
		throw new Exception("Cannot evaluate C-expr at compile time");
	}

	internal override string ToString(TValue vp)
	{
		return ToCExpr(vp.x);
	}

	internal string ToCExpr(int off)
	{
		if (off == 0) {
			return expr;
		} else if (off > 0) {
			return String.Format(
				"(uint32_t)({0}) + {1}", expr, off);
		} else {
			return String.Format(
				"(uint32_t)({0}) - {1}", expr, -(long)off);
		}
	}

	internal int GetMaxBitLength(int off)
	{
		long rmin = (long)min + off;
		long rmax = (long)max + off;
		int numBits = 1;
		if (rmin < 0) {
			numBits = Math.Max(numBits, BitLength(rmin));
		}
		if (rmax > 0) {
			numBits = Math.Max(numBits, BitLength(rmax));
		}
		return Math.Min(numBits, 32);
	}

	/*
	 * Get the minimal bit length of a value. This is for a signed
	 * representation: the length includes a sign bit. Thus, the
	 * returned value will be at least 1.
	 */
	static int BitLength(long v)
	{
		int num = 1;
		if (v < 0) {
			while (v != -1) {
				num ++;
				v >>= 1;
			}
		} else {
			while (v != 0) {
				num ++;
				v >>= 1;
			}
		}
		return num;
	}
}
