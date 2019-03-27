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

class CodeElementUIntExpr : CodeElement {

	uint val;
	TPointerExpr cx;
	int off;

	internal CodeElementUIntExpr(uint val,
		TPointerExpr cx, int off) : base()
	{
		this.val = val;
		this.cx = cx;
		this.off = off;
	}

	/* obsolete
	internal override int Length {
		get {
			return Encode7EUnsigned(val, null)
				+ (cx.GetMaxBitLength(off) + 6) / 7;
		}
	}
	*/

	internal override int GetLength(bool oneByteCode)
	{
		int len = oneByteCode ? 1 : Encode7EUnsigned(val, null);
		return len + (cx.GetMaxBitLength(off) + 6) / 7;
	}

	internal override int Encode(BlobWriter bw, bool oneByteCode)
	{
		int len1 = oneByteCode
			? EncodeOneByte(val, bw)
			: Encode7EUnsigned(val, bw);
		int len2 = (cx.GetMaxBitLength(off) + 6) / 7;
		bw.Append(String.Format("T0_INT{0}({1})",
			len2, cx.ToCExpr(off)));
		return len1 + len2;
	}
}
