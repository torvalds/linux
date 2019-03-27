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

abstract class CodeElement {

	internal int Address { get; set; }

	internal int LastLength { get; set; }

	// internal abstract int Length { get; }

	internal CodeElement()
	{
		Address = -1;
	}

	internal virtual void SetJumpTarget(CodeElement target)
	{
		throw new Exception("Code element accepts no target");
	}

	internal abstract int GetLength(bool oneByteCode);

	internal abstract int Encode(BlobWriter bw, bool oneByteCode);

	internal static int EncodeOneByte(uint val, BlobWriter bw)
	{
		if (val > 255) {
			throw new Exception(string.Format(
				"Cannot encode '{0}' over one byte", val));
		}
		bw.Append((byte)val);
		return 1;
	}

	internal static int Encode7EUnsigned(uint val, BlobWriter bw)
	{
		int len = 1;
		for (uint w = val; w >= 0x80; w >>= 7) {
			len ++;
		}
		if (bw != null) {
			for (int k = (len - 1) * 7; k >= 0; k -= 7) {
				int x = (int)(val >> k) & 0x7F;
				if (k > 0) {
					x |= 0x80;
				}
				bw.Append((byte)x);
			}
		}
		return len;
	}

	internal static int Encode7ESigned(int val, BlobWriter bw)
	{
		int len = 1;
		if (val < 0) {
			for (int w = val; w < -0x40; w >>= 7) {
				len ++;
			}
		} else {
			for (int w = val; w >= 0x40; w >>= 7) {
				len ++;
			}
		}
		if (bw != null) {
			for (int k = (len - 1) * 7; k >= 0; k -= 7) {
				int x = (int)(val >> k) & 0x7F;
				if (k > 0) {
					x |= 0x80;
				}
				bw.Append((byte)x);
			}
		}
		return len;
	}
}
