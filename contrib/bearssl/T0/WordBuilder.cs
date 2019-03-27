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
 * A WordBuilder instance organizes construction of a new interpreted word.
 *
 * Opcodes are accumulated with specific methods. A control-flow stack
 * is maintained to resolve jumps.
 *
 * Each instance shall be used for only one word.
 */

class WordBuilder {

	T0Comp TC;
	string name;
	int[] cfStack;
	int cfPtr;
	List<Opcode> code;
	List<string> toResolve;
	Dictionary<string, int> locals;
	bool jumpToLast;

	internal SType StackEffect {
		get; set;
	}

	/*
	 * Create a new instance, with the specified word name.
	 */
	internal WordBuilder(T0Comp TC, string name)
	{
		this.TC = TC;
		this.name = name;
		cfStack = new int[16];
		cfPtr = -1;
		code = new List<Opcode>();
		toResolve = new List<string>();
		locals = new Dictionary<string, int>();
		jumpToLast = true;
		StackEffect = SType.UNKNOWN;
	}

	/*
	 * Build the word. The control-flow stack must be empty. A 'ret'
	 * opcode is automatically appended if required.
	 */
	internal Word Build()
	{
		if (cfPtr != -1) {
			throw new Exception("control-flow stack is not empty");
		}
		if (jumpToLast || code[code.Count - 1].MayFallThrough) {
			Ret();
		}
		Word w = new WordInterpreted(TC, name, locals.Count,
			code.ToArray(), toResolve.ToArray());
		w.StackEffect = StackEffect;
		return w;
	}

	void Add(Opcode op)
	{
		Add(op, null);
	}

	void Add(Opcode op, string refName)
	{
		code.Add(op);
		toResolve.Add(refName);
		jumpToLast = false;
	}

	/*
	 * Rotate the control-flow stack at depth 'depth'.
	 */
	internal void CSRoll(int depth)
	{
		int x = cfStack[cfPtr - depth];
		Array.Copy(cfStack, cfPtr - (depth - 1),
			cfStack, cfPtr - depth, depth);
		cfStack[cfPtr] = x;
	}

	/*
	 * Make a copy of the control-flow element at depth 'depth', and
	 * push it on top of the control-flow stack.
	 */
	internal void CSPick(int depth)
	{
		int x = cfStack[cfPtr - depth];
		CSPush(x);
	}

	void CSPush(int x)
	{
		int len = cfStack.Length;
		if (++ cfPtr == len) {
			int[] ncf = new int[len << 1];
			Array.Copy(cfStack, 0, ncf, 0, len);
			cfStack = ncf;
		}
		cfStack[cfPtr] = x;
	}

	int CSPop()
	{
		return cfStack[cfPtr --];
	}

	/*
	 * Push an origin on the control-flow stack, corresponding to the
	 * next opcode to add.
	 */
	internal void CSPushOrig()
	{
		CSPush(code.Count);
	}

	/*
	 * Push a destination on the control-flow stack, corresponding to
	 * the next opcode to add.
	 */
	internal void CSPushDest()
	{
		CSPush(-code.Count - 1);
	}

	/*
	 * Pop an origin from the control-flow stack. An exception is
	 * thrown if the value is not an origin.
	 */
	internal int CSPopOrig()
	{
		int x = CSPop();
		if (x < 0) {
			throw new Exception("not an origin");
		}
		return x;
	}

	/*
	 * Pop a destination from the control-flow stack. An exception is
	 * thrown if the value is not a destination.
	 */
	internal int CSPopDest()
	{
		int x = CSPop();
		if (x >= 0) {
			throw new Exception("not a destination");
		}
		return -x - 1;
	}

	/*
	 * Add a "push literal" opcode.
	 */
	internal void Literal(TValue v)
	{
		Add(new OpcodeConst(v));
	}

	/*
	 * Compile a "call" by name. This method implements the support
	 * for local variables:
	 *
	 *  - If the target is '>' followed by a local variable name, then
	 *    a "put local" opcode is added.
	 *
	 *  - Otherwise, if the target is a local variable name, then a
	 *    "get local" opcode is added.
	 *
	 *  - Otherwise, a call to the named word is added. The target name
	 *    will be resolved later on (typically, when the word containing
	 *    the call opcode is first invoked, or when C code is generated).
	 */
	internal void Call(string target)
	{
		string lname;
		bool write;
		if (target.StartsWith(">")) {
			lname = target.Substring(1);
			write = true;
		} else {
			lname = target;
			write = false;
		}
		int lnum;
		if (locals.TryGetValue(lname, out lnum)) {
			if (write) {
				Add(new OpcodePutLocal(lnum));
			} else {
				Add(new OpcodeGetLocal(lnum));
			}
		} else {
			Add(new OpcodeCall(), target);
		}
	}

	/*
	 * Add a "call" opcode to the designated word.
	 */
	internal void CallExt(Word wtarget)
	{
		Add(new OpcodeCall(wtarget), null);
	}

	/*
	 * Add a "call" opcode to a word which is not currently resolved.
	 * This method ignores local variables.
	 */
	internal void CallExt(string target)
	{
		Add(new OpcodeCall(), target);
	}

	/*
	 * Add a "get local" opcode; the provided local name must already
	 * be defined.
	 */
	internal void GetLocal(string name)
	{
		int lnum;
		if (locals.TryGetValue(name, out lnum)) {
			Add(new OpcodeGetLocal(lnum));
		} else {
			throw new Exception("no such local: " + name);
		}
	}

	/*
	 * Add a "put local" opcode; the provided local name must already
	 * be defined.
	 */
	internal void PutLocal(string name)
	{
		int lnum;
		if (locals.TryGetValue(name, out lnum)) {
			Add(new OpcodePutLocal(lnum));
		} else {
			throw new Exception("no such local: " + name);
		}
	}

	/*
	 * Define a new local name.
	 */
	internal void DefLocal(string lname)
	{
		if (locals.ContainsKey(lname)) {
			throw new Exception(String.Format(
				"local already defined: {0}", lname));
		}
		locals[lname] = locals.Count;
	}

	/*
	 * Add a "call" opcode whose target is an XT value (which may be
	 * resolved or as yet unresolved).
	 */
	internal void Call(TPointerXT xt)
	{
		if (xt.Target == null) {
			Add(new OpcodeCall(), xt.Name);
		} else {
			Add(new OpcodeCall(xt.Target));
		}
	}

	/*
	 * Add a "ret" opcode.
	 */
	internal void Ret()
	{
		Add(new OpcodeRet());
	}

	/*
	 * Add a forward unconditional jump. The new opcode address is
	 * pushed on the control-flow stack as an origin.
	 */
	internal void Ahead()
	{
		CSPushOrig();
		Add(new OpcodeJumpUncond());
	}

	/*
	 * Add a forward conditional jump, which will be taken at runtime
	 * if the top-of-stack value is 'true'. The new opcode address is
	 * pushed on the control-flow stack as an origin.
	 */
	internal void AheadIf()
	{
		CSPushOrig();
		Add(new OpcodeJumpIf());
	}

	/*
	 * Add a forward conditional jump, which will be taken at runtime
	 * if the top-of-stack value is 'false'. The new opcode address is
	 * pushed on the control-flow stack as an origin.
	 */
	internal void AheadIfNot()
	{
		CSPushOrig();
		Add(new OpcodeJumpIfNot());
	}

	/*
	 * Resolve a previous forward jump to the current code address.
	 * The top of control-flow stack is popped and must be an origin.
	 */
	internal void Then()
	{
		int x = CSPopOrig();
		code[x].ResolveJump(code.Count - x - 1);
		jumpToLast = true;
	}

	/*
	 * Push the current code address on the control-flow stack as a
	 * destination, to be used by an ulterior backward jump.
	 */
	internal void Begin()
	{
		CSPushDest();
	}

	/*
	 * Add a backward unconditional jump. The jump target is popped
	 * from the control-flow stack as a destination.
	 */
	internal void Again()
	{
		int x = CSPopDest();
		Add(new OpcodeJumpUncond(x - code.Count - 1));
	}

	/*
	 * Add a backward conditional jump, which will be taken at runtime
	 * if the top-of-stack value is 'true'. The jump target is popped
	 * from the control-flow stack as a destination.
	 */
	internal void AgainIf()
	{
		int x = CSPopDest();
		Add(new OpcodeJumpIf(x - code.Count - 1));
	}

	/*
	 * Add a backward conditional jump, which will be taken at runtime
	 * if the top-of-stack value is 'false'. The jump target is popped
	 * from the control-flow stack as a destination.
	 */
	internal void AgainIfNot()
	{
		int x = CSPopDest();
		Add(new OpcodeJumpIfNot(x - code.Count - 1));
	}
}
