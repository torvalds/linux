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

class CodeElementJump : CodeElement {

	uint jumpType;
	CodeElement target;

	internal CodeElementJump(uint jumpType)
	{
		this.jumpType = jumpType;
	}

	/* obsolete
	internal override int Length {
		get {
			int len = Encode7EUnsigned(jumpType, null);
			int joff = JumpOff;
			if (joff == Int32.MinValue) {
				len ++;
			} else {
				len += Encode7ESigned(joff, null);
			}
			return len;
		}
	}
	*/

	internal override int GetLength(bool oneByteCode)
	{
		int len = oneByteCode ? 1 : Encode7EUnsigned(jumpType, null);
		int joff = JumpOff;
		if (joff == Int32.MinValue) {
			len ++;
		} else {
			len += Encode7ESigned(joff, null);
		}
		return len;
	}

	internal override void SetJumpTarget(CodeElement target)
	{
		this.target = target;
	}

	int JumpOff {
		get {
			if (target == null || Address < 0 || target.Address < 0)
			{
				return Int32.MinValue;
			} else {
				return target.Address - (Address + LastLength);
			}
		}
	}

	internal override int Encode(BlobWriter bw, bool oneByteCode)
	{
		if (bw == null) {
			return GetLength(oneByteCode);
		}
		int len;
		if (oneByteCode) {
			len = EncodeOneByte(jumpType, bw);
		} else {
			len = Encode7EUnsigned(jumpType, bw);
		}
		int joff = JumpOff;
		if (joff == Int32.MinValue) {
			throw new Exception("Unresolved addresses");
		}
		return len + Encode7ESigned(joff, bw);
	}
}
