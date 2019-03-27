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

/*
 * Each value is represented with a TValue structure. Integers use the 'x'
 * field, and 'ptr' is null; for pointers, the 'ptr' field is used, and the
 * 'x' is then an offset in the object represented by 'ptr'.
 */

struct TValue {

	internal int x;
	internal TPointerBase ptr;

	internal TValue(int x)
	{
		this.x = x;
		this.ptr = null;
	}

	internal TValue(uint x)
	{
		this.x = (int)x;
		this.ptr = null;
	}

	internal TValue(bool b)
	{
		this.x = b ? -1 : 0;
		this.ptr = null;
	}

	internal TValue(int x, TPointerBase ptr)
	{
		this.x = x;
		this.ptr = ptr;
	}

	/*
	 * Convert this value to a boolean; integer 0 and null pointer are
	 * 'false', other values are 'true'.
	 */
	internal bool Bool {
		get {
			if (ptr == null) {
				return x != 0;
			} else {
				return ptr.ToBool(this);
			}
		}
	}

	/*
	 * Get this value as an integer. Pointers cannot be converted to
	 * integers.
	 */
	internal int Int {
		get {
			if (ptr == null) {
				return x;
			}
			throw new Exception("not an integer: " + ToString());
		}
	}

	/*
	 * Get this value as an unsigned integer. This is the integer
	 * value, reduced modulo 2^32 in the 0..2^32-1 range.
	 */
	internal uint UInt {
		get {
			return (uint)Int;
		}
	}

	/*
	 * String format of integers uses decimal representation. For
	 * pointers, this depends on the pointed-to value.
	 */
	public override string ToString()
	{
		if (ptr == null) {
			return String.Format("{0}", x);
		} else {
			return ptr.ToString(this);
		}
	}

	/*
	 * If this value is an XT, then execute it. Otherwise, an exception
	 * is thrown.
	 */
	internal void Execute(T0Comp ctx, CPU cpu)
	{
		ToXT().Execute(ctx, cpu);
	}

	/*
	 * Convert this value to an XT. On failure, an exception is thrown.
	 */
	internal TPointerXT ToXT()
	{
		TPointerXT xt = ptr as TPointerXT;
		if (xt == null) {
			throw new Exception(
				"value is not an xt: " + ToString());
		}
		return xt;
	}

	/*
	 * Compare this value to another.
	 */
	internal bool Equals(TValue v)
	{
		if (x != v.x) {
			return false;
		}
		if (ptr == v.ptr) {
			return true;
		}
		if (ptr == null || v.ptr == null) {
			return false;
		}
		return ptr.Equals(v.ptr);
	}

	public static implicit operator TValue(bool val)
	{
		return new TValue(val);
	}

	public static implicit operator TValue(sbyte val)
	{
		return new TValue((int)val);
	}

	public static implicit operator TValue(byte val)
	{
		return new TValue((int)val);
	}

	public static implicit operator TValue(short val)
	{
		return new TValue((int)val);
	}

	public static implicit operator TValue(ushort val)
	{
		return new TValue((int)val);
	}

	public static implicit operator TValue(char val)
	{
		return new TValue((int)val);
	}

	public static implicit operator TValue(int val)
	{
		return new TValue((int)val);
	}

	public static implicit operator TValue(uint val)
	{
		return new TValue((int)val);
	}

	public static implicit operator bool(TValue v)
	{
		return v.Bool;
	}

	public static implicit operator sbyte(TValue v)
	{
		return (sbyte)v.Int;
	}

	public static implicit operator byte(TValue v)
	{
		return (byte)v.Int;
	}

	public static implicit operator short(TValue v)
	{
		return (short)v.Int;
	}

	public static implicit operator ushort(TValue v)
	{
		return (ushort)v.Int;
	}

	public static implicit operator char(TValue v)
	{
		return (char)v.Int;
	}

	public static implicit operator int(TValue v)
	{
		return (int)v.Int;
	}

	public static implicit operator uint(TValue v)
	{
		return (uint)v.Int;
	}
}
