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
 * Execution of code during compilation is done in a virtual CPU
 * incarnated by this class, that contains the relevant registers.
 *
 * Accesses to the data on the stack are mapped to accesses to an
 * internal array, with no explicit control on boundaries. Since the
 * internal array may be larger than the actual stack contents,
 * nonsensical accesses may still "work" to some extent. The whole
 * thing won't derail beyond the CLR VM, though.
 */

class CPU {

	/*
	 * Next instruction to execute is in ipBuf[ipOff].
	 */
	internal Opcode[] ipBuf;
	internal int ipOff;

	/*
	 * stackBuf and stackPtr implement the data stack. The system
	 * stack uses frames; 'rsp' points to the current top frame.
	 */
	TValue[] stackBuf;
	int stackPtr;
	Frame rsp;

	internal CPU()
	{
		stackBuf = new TValue[16];
		stackPtr = -1;
		rsp = null;
	}

	/*
	 * Enter a function, reserving space for 'numLocals' local variables.
	 */
	internal void Enter(Opcode[] code, int numLocals)
	{
		Frame f = new Frame(rsp, numLocals);
		rsp = f;
		f.savedIpBuf = ipBuf;
		f.savedIpOff = ipOff;
		ipBuf = code;
		ipOff = 0;
	}

	/*
	 * Exit the current function.
	 */
	internal void Exit()
	{
		ipBuf = rsp.savedIpBuf;
		ipOff = rsp.savedIpOff;
		rsp = rsp.upper;
	}

	/*
	 * Get the current stack depth (number of elements).
	 */
	internal int Depth {
		get {
			return stackPtr + 1;
		}
	}

	/*
	 * Pop a value from the stack.
	 */
	internal TValue Pop()
	{
		return stackBuf[stackPtr --];
	}

	/*
	 * Push a value on the stack.
	 */
	internal void Push(TValue v)
	{
		int len = stackBuf.Length;
		if (++ stackPtr == len) {
			TValue[] nbuf = new TValue[len << 1];
			Array.Copy(stackBuf, 0, nbuf, 0, len);
			stackBuf = nbuf;
		}
		stackBuf[stackPtr] = v;
	}

	/*
	 * Look at the value at depth 'depth' (0 is top of stack). The
	 * stack is unchanged.
	 */
	internal TValue Peek(int depth)
	{
		return stackBuf[stackPtr - depth];
	}

	/*
	 * Rotate the stack at depth 'depth': the value at that depth
	 * is moved to the top of stack.
	 */
	internal void Rot(int depth)
	{
		TValue v = stackBuf[stackPtr - depth];
		Array.Copy(stackBuf, stackPtr - (depth - 1),
			stackBuf, stackPtr - depth, depth);
		stackBuf[stackPtr] = v;
	}

	/*
	 * Inverse-rotate the stack at depth 'depth': the value at the
	 * top of stack is moved to that depth.
	 */
	internal void NRot(int depth)
	{
		TValue v = stackBuf[stackPtr];
		Array.Copy(stackBuf, stackPtr - depth,
			stackBuf, stackPtr - (depth - 1), depth);
		stackBuf[stackPtr - depth] = v;
	}

	/*
	 * Get the current contents of the local variable 'num'.
	 */
	internal TValue GetLocal(int num)
	{
		return rsp.locals[num];
	}

	/*
	 * Set the contents of the local variable 'num'.
	 */
	internal void PutLocal(int num, TValue v)
	{
		rsp.locals[num] = v;
	}

	/*
	 * The system stack really is a linked list of Frame instances.
	 */
	class Frame {

		internal Frame upper;
		internal Opcode[] savedIpBuf;
		internal int savedIpOff;
		internal TValue[] locals;

		internal Frame(Frame upper, int numLocals)
		{
			this.upper = upper;
			locals = new TValue[numLocals];
		}
	}
}
