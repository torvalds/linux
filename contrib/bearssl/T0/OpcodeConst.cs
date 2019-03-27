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

class OpcodeConst : Opcode {

	TValue val;

	internal OpcodeConst(TValue val)
	{
		this.val = val;
	}

	internal override void Run(CPU cpu)
	{
		cpu.Push(val);
	}

	internal override Word GetReference(T0Comp ctx)
	{
		TPointerXT xt = val.ptr as TPointerXT;
		if (xt == null) {
			return null;
		}
		xt.Resolve(ctx);
		return xt.Target;
	}

	internal override ConstData GetDataBlock(T0Comp ctx)
	{
		TPointerBlob bp = val.ptr as TPointerBlob;
		return bp == null ? null : bp.Blob;
	}

	internal override CodeElement ToCodeElement()
	{
		if (val.ptr == null) {
			return new CodeElementUIntInt(1, val.Int);
		}
		TPointerXT xt = val.ptr as TPointerXT;
		if (xt != null) {
			if (val.x != 0) {
				throw new Exception(
					"Cannot compile XT: non-zero offset");
			}
			return new CodeElementUIntInt(1, xt.Target.Slot);
		}
		TPointerBlob bp = val.ptr as TPointerBlob;
		if (bp != null) {
			return new CodeElementUIntInt(1,
				val.x + bp.Blob.Address);
		}
		TPointerExpr cx = val.ptr as TPointerExpr;
		if (cx != null) {
			return new CodeElementUIntExpr(1, cx, val.x);
		}
		throw new Exception(String.Format(
			"Cannot embed constant (type = {0})",
			val.ptr.GetType().FullName));
	}

	internal override int StackAction {
		get {
			return 1;
		}
	}

	public override string ToString()
	{
		return "const " + val.ToString();
	}
}
