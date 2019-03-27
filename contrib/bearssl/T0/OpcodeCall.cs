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

class OpcodeCall : Opcode {

	Word target;

	internal OpcodeCall() : this(null)
	{
	}

	internal OpcodeCall(Word target)
	{
		this.target = target;
	}

	internal override void ResolveTarget(Word target)
	{
		if (this.target != null) {
			throw new Exception("Opcode already resolved");
		}
		this.target = target;
	}

	internal override void Run(CPU cpu)
	{
		target.Run(cpu);
	}

	internal override Word GetReference(T0Comp ctx)
	{
		if (target == null) {
			throw new Exception("Unresolved call target");
		}
		return target;
	}

	internal override CodeElement ToCodeElement()
	{
		return new CodeElementUInt((uint)target.Slot);
	}

	public override string ToString()
	{
		return "call " + (target == null ? "UNRESOLVED" : target.Name);
	}
}
