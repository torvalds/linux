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

abstract class Opcode {

	internal Opcode()
	{
	}

	/*
	 * Execute this opcode.
	 */
	internal abstract void Run(CPU cpu);

	/*
	 * Resolve the target (word reference) for this opcode.
	 */
	internal virtual void ResolveTarget(Word target)
	{
		throw new Exception("Not a call opcode");
	}

	/*
	 * Resolve the jump offset for this opcode. Displacement is
	 * relative to the address of the opcode that immediately follows
	 * the jump code; thus, 0 implies no jump at all.
	 */
	internal virtual void ResolveJump(int disp)
	{
		throw new Exception("Not a jump opcode");
	}

	/*
	 * Get the Word that this opcode references; this can happen
	 * only with "call" and "const" opcodes. For all other opcodes,
	 * this method returns null.
	 */
	internal virtual Word GetReference(T0Comp ctx)
	{
		return null;
	}

	/*
	 * Get the data block that this opcode references; this can happen
	 * only with "const" opcodes. For all other opcodes, this method
	 * returns null.
	 */
	internal virtual ConstData GetDataBlock(T0Comp ctx)
	{
		return null;
	}

	/*
	 * Test whether this opcode may "fall through", i.e. execution
	 * may at least potentially proceed to the next opcode.
	 */
	internal virtual bool MayFallThrough {
		get {
			return true;
		}
	}

	/*
	 * Get jump displacement. For non-jump opcodes, this returns 0.
	 */
	internal virtual int JumpDisp {
		get {
			return 0;
		}
	}

	/*
	 * Get stack effect for this opcode (number of elements added to
	 * the stack, could be negative). For OpcodeCall, this returns
	 * 0.
	 */
	internal virtual int StackAction {
		get {
			return 0;
		}
	}

	internal abstract CodeElement ToCodeElement();

	/*
	 * This method is called for the CodeElement corresponding to
	 * this opcode, at gcode[off]; it is used to compute actual
	 * byte jump offsets when converting code to C.
	 */
	internal virtual void FixUp(CodeElement[] gcode, int off)
	{
	}
}
