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
 * The implementation for interpreted words.
 */

class WordInterpreted : Word {

	/*
	 * Get the number of local variables for this word.
	 */
	internal int NumLocals {
		get; private set;
	}

	/*
	 * Get the sequence of opcodes for this word.
	 */
	internal Opcode[] Code {
		get; private set;
	}

	string[] toResolve;

	internal WordInterpreted(T0Comp owner, string name,
		int numLocals, Opcode[] code, string[] toResolve)
		: base(owner, name)
	{
		this.Code = code;
		this.toResolve = toResolve;
		NumLocals = numLocals;
	}

	internal override void Resolve()
	{
		if (toResolve == null) {
			return;
		}
		for (int i = 0; i < toResolve.Length; i ++) {
			string tt = toResolve[i];
			if (tt == null) {
				continue;
			}
			Code[i].ResolveTarget(TC.Lookup(tt));
		}
		toResolve = null;
	}

	internal override void Run(CPU cpu)
	{
		Resolve();
		cpu.Enter(Code, NumLocals);
	}

	internal override List<Word> GetReferences()
	{
		Resolve();
		List<Word> r = new List<Word>();
		foreach (Opcode op in Code) {
			Word w = op.GetReference(TC);
			if (w != null) {
				r.Add(w);
			}
		}
		return r;
	}

	internal override List<ConstData> GetDataBlocks()
	{
		Resolve();
		List<ConstData> r = new List<ConstData>();
		foreach (Opcode op in Code) {
			ConstData cd = op.GetDataBlock(TC);
			if (cd != null) {
				r.Add(cd);
			}
		}
		return r;
	}

	internal override void GenerateCodeElements(List<CodeElement> dst)
	{
		Resolve();
		int n = Code.Length;
		CodeElement[] gcode = new CodeElement[n];
		for (int i = 0; i < n; i ++) {
			gcode[i] = Code[i].ToCodeElement();
		}
		for (int i = 0; i < n; i ++) {
			Code[i].FixUp(gcode, i);
		}
		dst.Add(new CodeElementUInt((uint)NumLocals));
		for (int i = 0; i < n; i ++) {
			dst.Add(gcode[i]);
		}
	}

	int flowAnalysis;
	int maxDataStack;
	int maxReturnStack;

	bool MergeSA(int[] sa, int j, int c)
	{
		if (sa[j] == Int32.MinValue) {
			sa[j] = c;
			return true;
		} else if (sa[j] != c) {
			throw new Exception(string.Format(
				"In word '{0}', offset {1}:"
				+ " stack action mismatch ({2} / {3})",
				Name, j, sa[j], c));
		} else {
			return false;
		}
	}

	internal override void AnalyseFlow()
	{
		switch (flowAnalysis) {
		case 0:
			break;
		case 1:
			return;
		default:
			throw new Exception("recursive call detected in '"
				+ Name + "'");
		}
		flowAnalysis = 2;
		int n = Code.Length;
		int[] sa = new int[n];
		for (int i = 0; i < n; i ++) {
			sa[i] = Int32.MinValue;
		}
		sa[0] = 0;
		int[] toExplore = new int[n];
		int tX = 0, tY = 0;
		int off = 0;

		int exitSA = Int32.MinValue;
		int mds = 0;
		int mrs = 0;

		int maxDepth = 0;

		for (;;) {
			Opcode op = Code[off];
			bool mft = op.MayFallThrough;
			int c = sa[off];
			int a;
			if (op is OpcodeCall) {
				Word w = op.GetReference(TC);
				w.AnalyseFlow();
				SType se = w.StackEffect;
				if (!se.IsKnown) {
					throw new Exception(string.Format(
						"call from '{0}' to '{1}'"
						+ " with unknown stack effect",
						Name, w.Name));
				}
				if (se.NoExit) {
					mft = false;
					a = 0;
				} else {
					a = se.DataOut - se.DataIn;
				}
				mds = Math.Max(mds, c + w.MaxDataStack);
				mrs = Math.Max(mrs, w.MaxReturnStack);
				maxDepth = Math.Min(maxDepth, c - se.DataIn);
			} else if (op is OpcodeRet) {
				if (exitSA == Int32.MinValue) {
					exitSA = c;
				} else if (exitSA != c) {
					throw new Exception(string.Format(
						"'{0}': exit stack action"
						+ " mismatch: {1} / {2}"
						+ " (offset {3})",
						Name, exitSA, c, off));
				}
				a = 0;
			} else {
				a = op.StackAction;
				mds = Math.Max(mds, c + a);
			}
			c += a;
			maxDepth = Math.Min(maxDepth, c);

			int j = op.JumpDisp;
			if (j != 0) {
				j += off + 1;
				toExplore[tY ++] = j;
				MergeSA(sa, j, c);
			}
			off ++;
			if (!mft || !MergeSA(sa, off, c)) {
				if (tX < tY) {
					off = toExplore[tX ++];
				} else {
					break;
				}
			}
		}

		maxDataStack = mds;
		maxReturnStack = 1 + NumLocals + mrs;

		/*
		 * TODO: see about this warning. Usage of a 'fail'
		 * word (that does not exit) within a 'case..endcase'
		 * structure will make an unreachable opcode. In a future
		 * version we might want to automatically remove dead
		 * opcodes.
		for (int i = 0; i < n; i ++) {
			if (sa[i] == Int32.MinValue) {
				Console.WriteLine("warning: word '{0}',"
					+ " offset {1}: unreachable opcode",
					Name, i);
				continue;
			}
		}
		 */

		SType computed;
		if (exitSA == Int32.MinValue) {
			computed = new SType(-maxDepth, -1);
		} else {
			computed = new SType(-maxDepth, -maxDepth + exitSA);
		}

		if (StackEffect.IsKnown) {
			if (!computed.IsSubOf(StackEffect)) {
				throw new Exception(string.Format(
					"word '{0}':"
					+ " computed stack effect {1}"
					+ " does not match declared {2}",
					Name, computed.ToString(),
					StackEffect.ToString()));
			}
		} else {
			StackEffect = computed;
		}

		flowAnalysis = 1;
	}

	internal override int MaxDataStack {
		get {
			AnalyseFlow();
			return maxDataStack;
		}
	}

	internal override int MaxReturnStack {
		get {
			AnalyseFlow();
			return maxReturnStack;
		}
	}
}
