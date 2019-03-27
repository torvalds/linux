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

abstract class OpcodeJump : Opcode {

	int disp;

	internal OpcodeJump() : this(Int32.MinValue)
	{
	}

	internal OpcodeJump(int disp)
	{
		this.disp = disp;
	}

	internal override int JumpDisp {
		get {
			return disp;
		}
	}

	internal override void Run(CPU cpu)
	{
		cpu.ipOff += disp;
	}

	internal override void ResolveJump(int disp)
	{
		if (this.disp != Int32.MinValue) {
			throw new Exception("Jump already resolved");
		}
		this.disp = disp;
	}

	internal override void FixUp(CodeElement[] gcode, int off)
	{
		gcode[off].SetJumpTarget(gcode[off + 1 + disp]);
	}
}
