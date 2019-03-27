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
using System.Text;

class ConstData {

	internal long ID { get; private set; }
	internal int Address { get; set; }
	internal int Length {
		get {
			return len;
		}
	}

	byte[] buf;
	int len;

	internal ConstData(T0Comp ctx)
	{
		ID = ctx.NextBlobID();
		buf = new byte[4];
		len = 0;
	}

	void Expand(int elen)
	{
		int tlen = len + elen;
		if (tlen > buf.Length) {
			int nlen = Math.Max(buf.Length << 1, tlen);
			byte[] nbuf = new byte[nlen];
			Array.Copy(buf, 0, nbuf, 0, len);
			buf = nbuf;
		}
	}

	internal void Add8(byte b)
	{
		Expand(1);
		buf[len ++] = b;
	}

	internal void Add16(int x)
	{
		Expand(2);
		buf[len ++] = (byte)(x >> 8);
		buf[len ++] = (byte)x;
	}

	internal void Add24(int x)
	{
		Expand(3);
		buf[len ++] = (byte)(x >> 16);
		buf[len ++] = (byte)(x >> 8);
		buf[len ++] = (byte)x;
	}

	internal void Add32(int x)
	{
		Expand(4);
		buf[len ++] = (byte)(x >> 24);
		buf[len ++] = (byte)(x >> 16);
		buf[len ++] = (byte)(x >> 8);
		buf[len ++] = (byte)x;
	}

	internal void AddString(string s)
	{
		byte[] sd = Encoding.UTF8.GetBytes(s);
		Expand(sd.Length + 1);
		Array.Copy(sd, 0, buf, len, sd.Length);
		buf[len + sd.Length] = 0;
		len += sd.Length + 1;
	}

	void CheckIndex(int off, int dlen)
	{
		if (off < 0 || off > (len - dlen)) {
			throw new IndexOutOfRangeException();
		}
	}

	internal void Set8(int off, byte v)
	{
		CheckIndex(off, 1);
		buf[off] = v;
	}

	internal byte Read8(int off)
	{
		CheckIndex(off, 1);
		return buf[off];
	}

	internal int Read16(int off)
	{
		CheckIndex(off, 2);
		return (buf[off] << 8) | buf[off + 1];
	}

	internal int Read24(int off)
	{
		CheckIndex(off, 3);
		return (buf[off] << 16) | (buf[off + 1] << 8) | buf[off + 2];
	}

	internal int Read32(int off)
	{
		CheckIndex(off, 4);
		return (buf[off] << 24) | (buf[off + 1] << 16)
			| (buf[off + 2] << 8) | buf[off + 3];
	}

	internal string ToString(int off)
	{
		StringBuilder sb = new StringBuilder();
		for (;;) {
			int x = DecodeUTF8(ref off);
			if (x == 0) {
				return sb.ToString();
			}
			if (x < 0x10000) {
				sb.Append((char)x);
			} else {
				x -= 0x10000;
				sb.Append((char)(0xD800 + (x >> 10)));
				sb.Append((char)(0xDC00 + (x & 0x3FF)));
			}
		}
	}

	int DecodeUTF8(ref int off)
	{
		if (off >= len) {
			throw new IndexOutOfRangeException();
		}
		int x = buf[off ++];
		if (x < 0xC0 || x > 0xF7) {
			return x;
		}
		int elen, acc;
		if (x >= 0xF0) {
			elen = 3;
			acc = x & 0x07;
		} else if (x >= 0xE0) {
			elen = 2;
			acc = x & 0x0F;
		} else {
			elen = 1;
			acc = x & 0x1F;
		}
		if (off + elen > len) {
			return x;
		}
		for (int i = 0; i < elen; i ++) {
			int y = buf[off + i];
			if (y < 0x80 || y >= 0xC0) {
				return x;
			}
			acc = (acc << 6) + (y & 0x3F);
		}
		if (acc > 0x10FFFF) {
			return x;
		}
		off += elen;
		return acc;
	}

	internal void Encode(BlobWriter bw)
	{
		for (int i = 0; i < len; i ++) {
			bw.Append(buf[i]);
		}
	}
}
