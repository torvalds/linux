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
 * A "word" is a function with a name. Words can be either native or
 * interpreted; native words are implemented as some in-compiler special
 * code.
 *
 * Some native words (not all of them) have a C implementation and can
 * thus be part of the generated C code. Native words with no C
 * implementation can be used only during compilation; this is typically
 * the case for words that support the syntax (e.g. 'if').
 */

abstract class Word {

	/*
	 * The compiler context for this word.
	 */
	internal T0Comp TC {
		get; private set;
	}

	/*
	 * Immediate words are executed immediately when encountered in the
	 * source code, even while compiling another word.
	 */
	internal bool Immediate {
		get; set;
	}

	/*
	 * Each word has a unique name. Names are case-sensitive.
	 */
	internal string Name {
		get; private set;
	}

	/*
	 * Words are allocated slot numbers when output code is generated.
	 */
	internal int Slot {
		get; set;
	}

	/*
	 * Each word may have a known stack effect.
	 */
	internal SType StackEffect {
		get; set;
	}

	internal Word(T0Comp owner, string name)
	{
		TC = owner;
		Name = name;
		StackEffect = SType.UNKNOWN;
	}

	/*
	 * Resolving a word means looking up all references to external
	 * words.
	 */
	internal virtual void Resolve()
	{
	}

	/*
	 * Execute this word. If the word is native, then its code is
	 * run right away; if the word is interpreted, then the entry
	 * sequence is executed.
	 */
	internal virtual void Run(CPU cpu)
	{
		throw new Exception(String.Format(
			"cannot run '{0}' at compile-time", Name));
	}

	/*
	 * All words may have an explicit C implementations. To be part
	 * of the generated C code, a word must either be interpreted,
	 * or have an explicit C implementation, or both.
	 */
	internal string CCode {
		get; set;
	}

	/*
	 * Get all words referenced from this one. This implies
	 * resolving the word.
	 */
	internal virtual List<Word> GetReferences()
	{
		return new List<Word>();
	}

	/*
	 * Get all data blocks directly referenced from this one. This
	 * implies resolving the word.
	 */
	internal virtual List<ConstData> GetDataBlocks()
	{
		return new List<ConstData>();
	}

	/*
	 * Produce the code elements for this word.
	 */
	internal virtual void GenerateCodeElements(List<CodeElement> dst)
	{
		throw new Exception("Word does not yield code elements");
	}

	/*
	 * Compute/verify stack effect for this word.
	 */
	internal virtual void AnalyseFlow()
	{
	}

	/*
	 * Get maximum data stack usage for this word. This is the number
	 * of extra slots that this word may need on the data stack. If
	 * the stack effect is not known, this returns -1.
	 */
	internal virtual int MaxDataStack {
		get {
			SType se = StackEffect;
			if (!se.IsKnown) {
				return -1;
			}
			if (se.NoExit) {
				return 0;
			} else {
				return Math.Min(0, se.DataOut - se.DataIn);
			}
		}
	}

	/*
	 * Get maximum return stack usage for this word.
	 */
	internal virtual int MaxReturnStack {
		get {
			return 0;
		}
	}
}
