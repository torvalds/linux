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

class TPointerBase {

	/* obsolete
	internal virtual TValue Get(TValue vp)
	{
		throw new Exception(
			"cannot get values from this pointer");
	}

	internal virtual void Set(TValue vp, TValue nval)
	{
		throw new Exception(
			"cannot set values to this pointer");
	}
	*/

	internal virtual bool ToBool(TValue vp)
	{
		return true;
	}

	internal virtual void Execute(T0Comp ctx, CPU cpu)
	{
		throw new Exception("value is not an xt: " + ToString());
	}

	internal virtual string ToString(TValue vp)
	{
		return String.Format("{0}+{1}",
			GetType().Name, vp.x);
	}

	internal virtual bool Equals(TPointerBase tp)
	{
		return this == tp;
	}
}
