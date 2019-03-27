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
using System.Collections.Generic;

class TPointerBlob : TPointerBase {

	internal ConstData Blob { get; private set; }

	internal TPointerBlob(ConstData cd)
	{
		this.Blob = cd;
	}

	internal TPointerBlob(T0Comp owner, string s)
	{
		Blob = new ConstData(owner);
		Blob.AddString(s);
	}

	/* obsolete
	internal override TValue Get8(TValue vp)
	{
		return new TValue((int)Blob.Read8(vp.x));
	}

	internal override TValue Get16(TValue vp)
	{
		return new TValue((int)Blob.Read16(vp.x));
	}

	internal override TValue Get24(TValue vp)
	{
		return new TValue((int)Blob.Read24(vp.x));
	}

	internal override TValue Get32(TValue vp)
	{
		return new TValue((int)Blob.Read32(vp.x));
	}
	*/

	internal override string ToString(TValue vp)
	{
		return Blob.ToString(vp.x);
	}

	internal override bool Equals(TPointerBase tp)
	{
		TPointerBlob tb = tp as TPointerBlob;
		return tb != null && Blob == tb.Blob;
	}
}
