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
using System.IO;
using System.Reflection;
using System.Text;

/*
 * This is the main compiler class.
 */

public class T0Comp {

	/*
	 * Command-line entry point.
	 */
	public static void Main(string[] args)
	{
		try {
			List<string> r = new List<string>();
			string outBase = null;
			List<string> entryPoints = new List<string>();
			string coreRun = null;
			bool flow = true;
			int dsLim = 32;
			int rsLim = 32;
			for (int i = 0; i < args.Length; i ++) {
				string a = args[i];
				if (!a.StartsWith("-")) {
					r.Add(a);
					continue;
				}
				if (a == "--") {
					for (;;) {
						if (++ i >= args.Length) {
							break;
						}
						r.Add(args[i]);
					}
					break;
				}
				while (a.StartsWith("-")) {
					a = a.Substring(1);
				}
				int j = a.IndexOf('=');
				string pname;
				string pval, pval2;
				if (j < 0) {
					pname = a.ToLowerInvariant();
					pval = null;
					pval2 = (i + 1) < args.Length
						? args[i + 1] : null;
				} else {
					pname = a.Substring(0, j).Trim()
						.ToLowerInvariant();
					pval = a.Substring(j + 1);
					pval2 = null;
				}
				switch (pname) {
				case "o":
				case "out":
					if (pval == null) {
						if (pval2 == null) {
							Usage();
						}
						i ++;
						pval = pval2;
					}
					if (outBase != null) {
						Usage();
					}
					outBase = pval;
					break;
				case "r":
				case "run":
					if (pval == null) {
						if (pval2 == null) {
							Usage();
						}
						i ++;
						pval = pval2;
					}
					coreRun = pval;
					break;
				case "m":
				case "main":
					if (pval == null) {
						if (pval2 == null) {
							Usage();
						}
						i ++;
						pval = pval2;
					}
					foreach (string ep in pval.Split(',')) {
						string epz = ep.Trim();
						if (epz.Length > 0) {
							entryPoints.Add(epz);
						}
					}
					break;
				case "nf":
				case "noflow":
					flow = false;
					break;
				default:
					Usage();
					break;
				}
			}
			if (r.Count == 0) {
				Usage();
			}
			if (outBase == null) {
				outBase = "t0out";
			}
			if (entryPoints.Count == 0) {
				entryPoints.Add("main");
			}
			if (coreRun == null) {
				coreRun = outBase;
			}
			T0Comp tc = new T0Comp();
			tc.enableFlowAnalysis = flow;
			tc.dsLimit = dsLim;
			tc.rsLimit = rsLim;
			using (TextReader tr = new StreamReader(
				Assembly.GetExecutingAssembly()
				.GetManifestResourceStream("t0-kernel")))
			{
				tc.ProcessInput(tr);
			}
			foreach (string a in r) {
				Console.WriteLine("[{0}]", a);
				using (TextReader tr = File.OpenText(a)) {
					tc.ProcessInput(tr);
				}
			}
			tc.Generate(outBase, coreRun, entryPoints.ToArray());
		} catch (Exception e) {
			Console.WriteLine(e.ToString());
			Environment.Exit(1);
		}
	}

	static void Usage()
	{
		Console.WriteLine(
"usage: T0Comp.exe [ options... ] file...");
		Console.WriteLine(
"options:");
		Console.WriteLine(
"   -o file    use 'file' as base for output file name (default: 't0out')");
		Console.WriteLine(
"   -r name    use 'name' as base for run function (default: same as output)");
		Console.WriteLine(
"   -m name[,name...]");
		Console.WriteLine(
"              define entry point(s)");
		Console.WriteLine(
"   -nf        disable flow analysis");
		Environment.Exit(1);
	}

	/*
	 * If 'delayedChar' is Int32.MinValue then there is no delayed
	 * character.
	 * If 'delayedChar' equals x >= 0 then there is one delayed
	 * character of value x.
	 * If 'delayedChar' equals y < 0 then there are two delayed
	 * characters, a newline (U+000A) followed by character of
	 * value -(y+1).
	 */
	TextReader currentInput;
	int delayedChar;

	/*
	 * Common StringBuilder used to parse tokens; it is reused for
	 * each new token.
	 */
	StringBuilder tokenBuilder;

	/*
	 * There may be a delayed token in some cases.
	 */
	String delayedToken;

	/*
	 * Defined words are referenced by name in this map. Names are
	 * string-sensitive; for better reproducibility, the map is sorted
	 * (ordinal order).
	 */
	IDictionary<string, Word> words;

	/*
	 * Last defined word is also referenced in 'lastWord'. This is
	 * used by 'immediate'.
	 */
	Word lastWord;

	/*
	 * When compiling, this builder is used. A stack saves other
	 * builders in case of nested definition.
	 */
	WordBuilder wordBuilder;
	Stack<WordBuilder> savedWordBuilders;

	/*
	 * C code defined for words is kept in this map, by word name.
	 */
	IDictionary<string, string> allCCode;

	/*
	 * 'compiling' is true when compiling tokens to a word, false
	 * when interpreting them.
	 */
	bool compiling;

	/*
	 * 'quitRunLoop' is set to true to trigger exit of the
	 * interpretation loop when the end of the current input file
	 * is reached.
	 */
	bool quitRunLoop;

	/*
	 * 'extraCode' is for C code that is to be added as preamble to
	 * the C output.
	 */
	List<string> extraCode;

	/*
	 * 'extraCodeDefer' is for C code that is to be added in the C
	 * output _after_ the data and code blocks.
	 */
	List<string> extraCodeDefer;

	/*
	 * 'dataBlock' is the data block in which constant data bytes
	 * are accumulated.
	 */
	ConstData dataBlock;

	/*
	 * Counter for blocks of constant data.
	 */
	long currentBlobID;

	/*
	 * Flow analysis enable flag.
	 */
	bool enableFlowAnalysis;

	/*
	 * Data stack size limit.
	 */
	int dsLimit;

	/*
	 * Return stack size limit.
	 */
	int rsLimit;

	T0Comp()
	{
		tokenBuilder = new StringBuilder();
		words = new SortedDictionary<string, Word>(
			StringComparer.Ordinal);
		savedWordBuilders = new Stack<WordBuilder>();
		allCCode = new SortedDictionary<string, string>(
			StringComparer.Ordinal);
		compiling = false;
		extraCode = new List<string>();
		extraCodeDefer = new List<string>();
		enableFlowAnalysis = true;

		/*
		 * Native words are predefined and implemented only with
		 * native code. Some may be part of the generated output,
		 * if C code is set for them.
		 */

		/*
		 * add-cc:
		 * Parses next token as a word name, then a C code snippet.
		 * Sets the C code for that word.
		 */
		AddNative("add-cc:", false, SType.BLANK, cpu => {
			string tt = Next();
			if (tt == null) {
				throw new Exception(
					"EOF reached (missing name)");
			}
			if (allCCode.ContainsKey(tt)) {
				throw new Exception(
					"C code already set for: " + tt);
			}
			allCCode[tt] = ParseCCode();
		});

		/*
		 * cc:
		 * Parses next token as a word name, then a C code snippet.
		 * A new word is defined, that throws an exception when
		 * invoked during compilation. The C code is set for that
		 * new word.
		 */
		AddNative("cc:", false, SType.BLANK, cpu => {
			string tt = Next();
			if (tt == null) {
				throw new Exception(
					"EOF reached (missing name)");
			}
			Word w = AddNative(tt, false, cpu2 => {
				throw new Exception(
					"C-only word: " + tt);
			});
			if (allCCode.ContainsKey(tt)) {
				throw new Exception(
					"C code already set for: " + tt);
			}
			SType stackEffect;
			allCCode[tt] = ParseCCode(out stackEffect);
			w.StackEffect = stackEffect;
		});

		/*
		 * preamble
		 * Parses a C code snippet, then adds it to the generated
		 * output preamble.
		 */
		AddNative("preamble", false, SType.BLANK, cpu => {
			extraCode.Add(ParseCCode());
		});

		/*
		 * postamble
		 * Parses a C code snippet, then adds it to the generated
		 * output after the data and code blocks.
		 */
		AddNative("postamble", false, SType.BLANK, cpu => {
			extraCodeDefer.Add(ParseCCode());
		});

		/*
		 * make-CX
		 * Expects two integers and a string, and makes a
		 * constant that stands for the string as a C constant
		 * expression. The two integers are the expected range
		 * (min-max, inclusive).
		 */
		AddNative("make-CX", false, new SType(3, 1), cpu => {
			TValue c = cpu.Pop();
			if (!(c.ptr is TPointerBlob)) {
				throw new Exception(string.Format(
					"'{0}' is not a string", c));
			}
			int max = cpu.Pop();
			int min = cpu.Pop();
			TValue tv = new TValue(0, new TPointerExpr(
				c.ToString(), min, max));
			cpu.Push(tv);
		});

		/*
		 * CX  (immediate)
		 * Parses two integer constants, then a C code snippet.
		 * It then pushes on the stack, or compiles to the
		 * current word, a value consisting of the given C
		 * expression; the two integers indicate the expected
		 * range (min-max, inclusive) of the C expression when
		 * evaluated.
		 */
		AddNative("CX", true, cpu => {
			string tt = Next();
			if (tt == null) {
				throw new Exception(
					"EOF reached (missing min value)");
			}
			int min = ParseInteger(tt);
			tt = Next();
			if (tt == null) {
				throw new Exception(
					"EOF reached (missing max value)");
			}
			int max = ParseInteger(tt);
			if (max < min) {
				throw new Exception("min/max in wrong order");
			}
			TValue tv = new TValue(0, new TPointerExpr(
				ParseCCode().Trim(), min, max));
			if (compiling) {
				wordBuilder.Literal(tv);
			} else {
				cpu.Push(tv);
			}
		});

		/*
		 * co
		 * Interrupt the current execution. This implements
		 * coroutines. It cannot be invoked during compilation.
		 */
		AddNative("co", false, SType.BLANK, cpu => {
			throw new Exception("No coroutine in compile mode");
		});

		/*
		 * :
		 * Parses next token as word name. It begins definition
		 * of that word, setting it as current target for
		 * word building. Any previously opened word is saved
		 * and will become available again as a target when that
		 * new word is finished building.
		 */
		AddNative(":", false, cpu => {
			string tt = Next();
			if (tt == null) {
				throw new Exception(
					"EOF reached (missing name)");
			}
			if (compiling) {
				savedWordBuilders.Push(wordBuilder);
			} else {
				compiling = true;
			}
			wordBuilder = new WordBuilder(this, tt);
			tt = Next();
			if (tt == null) {
				throw new Exception(
					"EOF reached (while compiling)");
			}
			if (tt == "(") {
				SType stackEffect = ParseStackEffectNF();
				if (!stackEffect.IsKnown) {
					throw new Exception(
						"Invalid stack effect syntax");
				}
				wordBuilder.StackEffect = stackEffect;
			} else {
				delayedToken = tt;
			}
		});

		/*
		 * Pops a string as word name, and two integers as stack
		 * effect. It begins definition of that word, setting it
		 * as current target for word building. Any previously
		 * opened word is saved and will become available again as
		 * a target when that new word is finished building.
		 *
		 * Stack effect is the pair 'din dout'. If din is negative,
		 * then the stack effect is "unknown". If din is nonnegative
		 * but dout is negative, then the word is reputed never to
		 * return.
		 */
		AddNative("define-word", false, cpu => {
			int dout = cpu.Pop();
			int din = cpu.Pop();
			TValue s = cpu.Pop();
			if (!(s.ptr is TPointerBlob)) {
				throw new Exception(string.Format(
					"Not a string: '{0}'", s));
			}
			string tt = s.ToString();
			if (compiling) {
				savedWordBuilders.Push(wordBuilder);
			} else {
				compiling = true;
			}
			wordBuilder = new WordBuilder(this, tt);
			wordBuilder.StackEffect = new SType(din, dout);
		});

		/*
		 * ;  (immediate)
		 * Ends current word. The current word is registered under
		 * its name, and the previously opened word (if any) becomes
		 * again the building target.
		 */
		AddNative(";", true, cpu => {
			if (!compiling) {
				throw new Exception("Not compiling");
			}
			Word w = wordBuilder.Build();
			string name = w.Name;
			if (words.ContainsKey(name)) {
				throw new Exception(
					"Word already defined: " + name);
			}
			words[name] = w;
			lastWord = w;
			if (savedWordBuilders.Count > 0) {
				wordBuilder = savedWordBuilders.Pop();
			} else {
				wordBuilder = null;
				compiling = false;
			}
		});

		/*
		 * immediate
		 * Sets the last defined word as immediate.
		 */
		AddNative("immediate", false, cpu => {
			if (lastWord == null) {
				throw new Exception("No word defined yet");
			}
			lastWord.Immediate = true;
		});

		/*
		 * literal  (immediate)
		 * Pops the current TOS value, and add in the current word
		 * the action of pushing that value. This cannot be used
		 * when no word is being built.
		 */
		WordNative wliteral = AddNative("literal", true, cpu => {
			CheckCompiling();
			wordBuilder.Literal(cpu.Pop());
		});

		/*
		 * compile
		 * Pops the current TOS value, which must be an XT (pointer
		 * to a word); the action of calling that word is compiled
		 * in the current word.
		 */
		WordNative wcompile = AddNative("compile", false, cpu => {
			CheckCompiling();
			wordBuilder.Call(cpu.Pop().ToXT());
		});

		/*
		 * postpone  (immediate)
		 * Parses the next token as a word name, and add to the
		 * current word the action of calling that word. This
		 * basically removes immediatety from the next word.
		 */
		AddNative("postpone", true, cpu => {
			CheckCompiling();
			string tt = Next();
			if (tt == null) {
				throw new Exception(
					"EOF reached (missing name)");
			}
			TValue v;
			bool isVal = TryParseLiteral(tt, out v);
			Word w = LookupNF(tt);
			if (isVal && w != null) {
				throw new Exception(String.Format(
					"Ambiguous: both defined word and"
					+ " literal: {0}", tt));
			}
			if (isVal) {
				wordBuilder.Literal(v);
				wordBuilder.CallExt(wliteral);
			} else if (w != null) {
				if (w.Immediate) {
					wordBuilder.CallExt(w);
				} else {
					wordBuilder.Literal(new TValue(0,
						new TPointerXT(w)));
					wordBuilder.CallExt(wcompile);
				}
			} else {
				wordBuilder.Literal(new TValue(0,
					new TPointerXT(tt)));
				wordBuilder.CallExt(wcompile);
			}
		});

		/*
		 * Interrupt compilation with an error.
		 */
		AddNative("exitvm", false, cpu => {
			throw new Exception();
		});

		/*
		 * Open a new data block. Its symbolic address is pushed
		 * on the stack.
		 */
		AddNative("new-data-block", false, cpu => {
			dataBlock = new ConstData(this);
			cpu.Push(new TValue(0, new TPointerBlob(dataBlock)));
		});

		/*
		 * Define a new data word. The data address and name are
		 * popped from the stack.
		 */
		AddNative("define-data-word", false, cpu => {
			string name = cpu.Pop().ToString();
			TValue va = cpu.Pop();
			TPointerBlob tb = va.ptr as TPointerBlob;
			if (tb == null) {
				throw new Exception(
					"Address is not a data area");
			}
			Word w = new WordData(this, name, tb.Blob, va.x);
			if (words.ContainsKey(name)) {
				throw new Exception(
					"Word already defined: " + name);
			}
			words[name] = w;
			lastWord = w;
		});

		/*
		 * Get an address pointing at the end of the current
		 * data block. This is the address of the next byte that
		 * will be added.
		 */
		AddNative("current-data", false, cpu => {
			if (dataBlock == null) {
				throw new Exception(
					"No current data block");
			}
			cpu.Push(new TValue(dataBlock.Length,
				new TPointerBlob(dataBlock)));
		});

		/*
		 * Add a byte value to the data block.
		 */
		AddNative("data-add8", false, cpu => {
			if (dataBlock == null) {
				throw new Exception(
					"No current data block");
			}
			int v = cpu.Pop();
			if (v < 0 || v > 0xFF) {
				throw new Exception(
					"Byte value out of range: " + v);
			}
			dataBlock.Add8((byte)v);
		});

		/*
		 * Set a byte value in the data block.
		 */
		AddNative("data-set8", false, cpu => {
			TValue va = cpu.Pop();
			TPointerBlob tb = va.ptr as TPointerBlob;
			if (tb == null) {
				throw new Exception(
					"Address is not a data area");
			}
			int v = cpu.Pop();
			if (v < 0 || v > 0xFF) {
				throw new Exception(
					"Byte value out of range: " + v);
			}
			tb.Blob.Set8(va.x, (byte)v);
		});

		/*
		 * Get a byte value from a data block.
		 */
		AddNative("data-get8", false, new SType(1, 1), cpu => {
			TValue va = cpu.Pop();
			TPointerBlob tb = va.ptr as TPointerBlob;
			if (tb == null) {
				throw new Exception(
					"Address is not a data area");
			}
			int v = tb.Blob.Read8(va.x);
			cpu.Push(v);
		});

		/*
		 *
		 */
		AddNative("compile-local-read", false, cpu => {
			CheckCompiling();
			wordBuilder.GetLocal(cpu.Pop().ToString());
		});
		AddNative("compile-local-write", false, cpu => {
			CheckCompiling();
			wordBuilder.PutLocal(cpu.Pop().ToString());
		});

		AddNative("ahead", true, cpu => {
			CheckCompiling();
			wordBuilder.Ahead();
		});
		AddNative("begin", true, cpu => {
			CheckCompiling();
			wordBuilder.Begin();
		});
		AddNative("again", true, cpu => {
			CheckCompiling();
			wordBuilder.Again();
		});
		AddNative("until", true, cpu => {
			CheckCompiling();
			wordBuilder.AgainIfNot();
		});
		AddNative("untilnot", true, cpu => {
			CheckCompiling();
			wordBuilder.AgainIf();
		});
		AddNative("if", true, cpu => {
			CheckCompiling();
			wordBuilder.AheadIfNot();
		});
		AddNative("ifnot", true, cpu => {
			CheckCompiling();
			wordBuilder.AheadIf();
		});
		AddNative("then", true, cpu => {
			CheckCompiling();
			wordBuilder.Then();
		});
		AddNative("cs-pick", false, cpu => {
			CheckCompiling();
			wordBuilder.CSPick(cpu.Pop());
		});
		AddNative("cs-roll", false, cpu => {
			CheckCompiling();
			wordBuilder.CSRoll(cpu.Pop());
		});
		AddNative("next-word", false, cpu => {
			string s = Next();
			if (s == null) {
				throw new Exception("No next word (EOF)");
			}
			cpu.Push(StringToBlob(s));
		});
		AddNative("parse", false, cpu => {
			int d = cpu.Pop();
			string s = ReadTerm(d);
			cpu.Push(StringToBlob(s));
		});
		AddNative("char", false, cpu => {
			int c = NextChar();
			if (c < 0) {
				throw new Exception("No next character (EOF)");
			}
			cpu.Push(c);
		});
		AddNative("'", false, cpu => {
			string name = Next();
			cpu.Push(new TValue(0, new TPointerXT(name)));
		});

		/*
		 * The "execute" word is valid in generated C code, but
		 * since it jumps to a runtime pointer, its actual stack
		 * effect cannot be computed in advance.
		 */
		AddNative("execute", false, cpu => {
			cpu.Pop().Execute(this, cpu);
		});

		AddNative("[", true, cpu => {
			CheckCompiling();
			compiling = false;
		});
		AddNative("]", false, cpu => {
			compiling = true;
		});
		AddNative("(local)", false, cpu => {
			CheckCompiling();
			wordBuilder.DefLocal(cpu.Pop().ToString());
		});
		AddNative("ret", true, cpu => {
			CheckCompiling();
			wordBuilder.Ret();
		});

		AddNative("drop", false, new SType(1, 0), cpu => {
			cpu.Pop();
		});
		AddNative("dup", false, new SType(1, 2), cpu => {
			cpu.Push(cpu.Peek(0));
		});
		AddNative("swap", false, new SType(2, 2), cpu => {
			cpu.Rot(1);
		});
		AddNative("over", false, new SType(2, 3), cpu => {
			cpu.Push(cpu.Peek(1));
		});
		AddNative("rot", false, new SType(3, 3), cpu => {
			cpu.Rot(2);
		});
		AddNative("-rot", false, new SType(3, 3), cpu => {
			cpu.NRot(2);
		});

		/*
		 * "roll" and "pick" are special in that the stack slot
		 * they inspect might be known only at runtime, so an
		 * absolute stack effect cannot be attributed. Instead,
		 * we simply hope that the caller knows what it is doing,
		 * and we use a simple stack effect for just the count
		 * value and picked value.
		 */
		AddNative("roll", false, new SType(1, 0), cpu => {
			cpu.Rot(cpu.Pop());
		});
		AddNative("pick", false, new SType(1, 1), cpu => {
			cpu.Push(cpu.Peek(cpu.Pop()));
		});

		AddNative("+", false, new SType(2, 1), cpu => {
			TValue b = cpu.Pop();
			TValue a = cpu.Pop();
			if (b.ptr == null) {
				a.x += (int)b;
				cpu.Push(a);
			} else if (a.ptr is TPointerBlob
				&& b.ptr is TPointerBlob)
			{
				cpu.Push(StringToBlob(
					a.ToString() + b.ToString()));
			} else {
				throw new Exception(string.Format(
					"Cannot add '{0}' to '{1}'", b, a));
			}
		});
		AddNative("-", false, new SType(2, 1), cpu => {
			/*
			 * We can subtract two pointers, provided that
			 * they point to the same blob. Otherwise,
			 * the subtraction second operand must be an
			 * integer.
			 */
			TValue b = cpu.Pop();
			TValue a = cpu.Pop();
			TPointerBlob ap = a.ptr as TPointerBlob;
			TPointerBlob bp = b.ptr as TPointerBlob;
			if (ap != null && bp != null && ap.Blob == bp.Blob) {
				cpu.Push(new TValue(a.x - b.x));
				return;
			}
			int bx = b;
			a.x -= bx;
			cpu.Push(a);
		});
		AddNative("neg", false, new SType(1, 1), cpu => {
			int ax = cpu.Pop();
			cpu.Push(-ax);
		});
		AddNative("*", false, new SType(2, 1), cpu => {
			int bx = cpu.Pop();
			int ax = cpu.Pop();
			cpu.Push(ax * bx);
		});
		AddNative("/", false, new SType(2, 1), cpu => {
			int bx = cpu.Pop();
			int ax = cpu.Pop();
			cpu.Push(ax / bx);
		});
		AddNative("u/", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop();
			uint ax = cpu.Pop();
			cpu.Push(ax / bx);
		});
		AddNative("%", false, new SType(2, 1), cpu => {
			int bx = cpu.Pop();
			int ax = cpu.Pop();
			cpu.Push(ax % bx);
		});
		AddNative("u%", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop();
			uint ax = cpu.Pop();
			cpu.Push(ax % bx);
		});
		AddNative("<", false, new SType(2, 1), cpu => {
			int bx = cpu.Pop();
			int ax = cpu.Pop();
			cpu.Push(ax < bx);
		});
		AddNative("<=", false, new SType(2, 1), cpu => {
			int bx = cpu.Pop();
			int ax = cpu.Pop();
			cpu.Push(ax <= bx);
		});
		AddNative(">", false, new SType(2, 1), cpu => {
			int bx = cpu.Pop();
			int ax = cpu.Pop();
			cpu.Push(ax > bx);
		});
		AddNative(">=", false, new SType(2, 1), cpu => {
			int bx = cpu.Pop();
			int ax = cpu.Pop();
			cpu.Push(ax >= bx);
		});
		AddNative("=", false, new SType(2, 1), cpu => {
			TValue b = cpu.Pop();
			TValue a = cpu.Pop();
			cpu.Push(a.Equals(b));
		});
		AddNative("<>", false, new SType(2, 1), cpu => {
			TValue b = cpu.Pop();
			TValue a = cpu.Pop();
			cpu.Push(!a.Equals(b));
		});
		AddNative("u<", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop().UInt;
			uint ax = cpu.Pop().UInt;
			cpu.Push(new TValue(ax < bx));
		});
		AddNative("u<=", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop().UInt;
			uint ax = cpu.Pop().UInt;
			cpu.Push(new TValue(ax <= bx));
		});
		AddNative("u>", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop().UInt;
			uint ax = cpu.Pop().UInt;
			cpu.Push(new TValue(ax > bx));
		});
		AddNative("u>=", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop();
			uint ax = cpu.Pop();
			cpu.Push(ax >= bx);
		});
		AddNative("and", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop();
			uint ax = cpu.Pop();
			cpu.Push(ax & bx);
		});
		AddNative("or", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop();
			uint ax = cpu.Pop();
			cpu.Push(ax | bx);
		});
		AddNative("xor", false, new SType(2, 1), cpu => {
			uint bx = cpu.Pop();
			uint ax = cpu.Pop();
			cpu.Push(ax ^ bx);
		});
		AddNative("not", false, new SType(1, 1), cpu => {
			uint ax = cpu.Pop();
			cpu.Push(~ax);
		});
		AddNative("<<", false, new SType(2, 1), cpu => {
			int count = cpu.Pop();
			if (count < 0 || count > 31) {
				throw new Exception("Invalid shift count");
			}
			uint ax = cpu.Pop();
			cpu.Push(ax << count);
		});
		AddNative(">>", false, new SType(2, 1), cpu => {
			int count = cpu.Pop();
			if (count < 0 || count > 31) {
				throw new Exception("Invalid shift count");
			}
			int ax = cpu.Pop();
			cpu.Push(ax >> count);
		});
		AddNative("u>>", false, new SType(2, 1), cpu => {
			int count = cpu.Pop();
			if (count < 0 || count > 31) {
				throw new Exception("Invalid shift count");
			}
			uint ax = cpu.Pop();
			cpu.Push(ax >> count);
		});

		AddNative(".", false, new SType(1, 0), cpu => {
			Console.Write(" {0}", cpu.Pop().ToString());
		});
		AddNative(".s", false, SType.BLANK, cpu => {
			int n = cpu.Depth;
			for (int i = n - 1; i >= 0; i --) {
				Console.Write(" {0}", cpu.Peek(i).ToString());
			}
		});
		AddNative("putc", false, new SType(1, 0), cpu => {
			Console.Write((char)cpu.Pop());
		});
		AddNative("puts", false, new SType(1, 0), cpu => {
			Console.Write("{0}", cpu.Pop().ToString());
		});
		AddNative("cr", false, SType.BLANK, cpu => {
			Console.WriteLine();
		});
		AddNative("eqstr", false, new SType(2, 1), cpu => {
			string s2 = cpu.Pop().ToString();
			string s1 = cpu.Pop().ToString();
			cpu.Push(s1 == s2);
		});
	}

	WordNative AddNative(string name, bool immediate,
		WordNative.NativeRun code)
	{
		return AddNative(name, immediate, SType.UNKNOWN, code);
	}

	WordNative AddNative(string name, bool immediate, SType stackEffect,
		WordNative.NativeRun code)
	{
		if (words.ContainsKey(name)) {
			throw new Exception(
				"Word already defined: " + name);
		}
		WordNative w = new WordNative(this, name, code);
		w.Immediate = immediate;
		w.StackEffect = stackEffect;
		words[name] = w;
		return w;
	}

	internal long NextBlobID()
	{
		return currentBlobID ++;
	}

	int NextChar()
	{
		int c = delayedChar;
		if (c >= 0) {
			delayedChar = Int32.MinValue;
		} else if (c > Int32.MinValue) {
			delayedChar = -(c + 1);
			c = '\n';
		} else {
			c = currentInput.Read();
		}
		if (c == '\r') {
			if (delayedChar >= 0) {
				c = delayedChar;
				delayedChar = Int32.MinValue;
			} else {
				c = currentInput.Read();
			}
			if (c != '\n') {
				delayedChar = c;
				c = '\n';
			}
		}
		return c;
	}

	/*
	 * Un-read the character value 'c'. That value MUST be the one
	 * that was obtained from NextChar().
	 */
	void Unread(int c)
	{
		if (c < 0) {
			return;
		}
		if (delayedChar < 0) {
			if (delayedChar != Int32.MinValue) {
				throw new Exception(
					"Already two delayed characters");
			}
			delayedChar = c;
		} else if (c != '\n') {
			throw new Exception("Cannot delay two characters");
		} else {
			delayedChar = -(delayedChar + 1);
		}
	}

	string Next()
	{
		string r = delayedToken;
		if (r != null) {
			delayedToken = null;
			return r;
		}
		tokenBuilder.Length = 0;
		int c;
		for (;;) {
			c = NextChar();
			if (c < 0) {
				return null;
			}
			if (!IsWS(c)) {
				break;
			}
		}
		if (c == '"') {
			return ParseString();
		}
		for (;;) {
			tokenBuilder.Append((char)c);
			c = NextChar();
			if (c < 0 || IsWS(c)) {
				Unread(c);
				return tokenBuilder.ToString();
			}
		}
	}

	string ParseCCode()
	{
		SType stackEffect;
		string r = ParseCCode(out stackEffect);
		if (stackEffect.IsKnown) {
			throw new Exception(
				"Stack effect forbidden in this declaration");
		}
		return r;
	}

	string ParseCCode(out SType stackEffect)
	{
		string s = ParseCCodeNF(out stackEffect);
		if (s == null) {
			throw new Exception("Error while parsing C code");
		}
		return s;
	}

	string ParseCCodeNF(out SType stackEffect)
	{
		stackEffect = SType.UNKNOWN;
		for (;;) {
			int c = NextChar();
			if (c < 0) {
				return null;
			}
			if (!IsWS(c)) {
				if (c == '(') {
					if (stackEffect.IsKnown) {
						Unread(c);
						return null;
					}
					stackEffect = ParseStackEffectNF();
					if (!stackEffect.IsKnown) {
						return null;
					}
					continue;
				} else if (c != '{') {
					Unread(c);
					return null;
				}
				break;
			}
		}
		StringBuilder sb = new StringBuilder();
		int count = 1;
		for (;;) {
			int c = NextChar();
			if (c < 0) {
				return null;
			}
			switch (c) {
			case '{':
				count ++;
				break;
			case '}':
				if (-- count == 0) {
					return sb.ToString();
				}
				break;
			}
			sb.Append((char)c);
		}
	}

	/*
	 * Parse a stack effect declaration. This method assumes that the
	 * opening parenthesis has just been read. If the parsing fails,
	 * then this method returns SType.UNKNOWN.
	 */
	SType ParseStackEffectNF()
	{
		bool seenSep = false;
		bool seenBang = false;
		int din = 0, dout = 0;
		for (;;) {
			string t = Next();
			if (t == null) {
				return SType.UNKNOWN;
			}
			if (t == "--") {
				if (seenSep) {
					return SType.UNKNOWN;
				}
				seenSep = true;
			} else if (t == ")") {
				if (seenSep) {
					if (seenBang && dout == 1) {
						dout = -1;
					}
					return new SType(din, dout);
				} else {
					return SType.UNKNOWN;
				}
			} else {
				if (seenSep) {
					if (dout == 0 && t == "!") {
						seenBang = true;
					}
					dout ++;
				} else {
					din ++;
				}
			}
		}
	}

	string ParseString()
	{
		StringBuilder sb = new StringBuilder();
		sb.Append('"');
		bool lcwb = false;
		int hexNum = 0;
		int acc = 0;
		for (;;) {
			int c = NextChar();
			if (c < 0) {
				throw new Exception(
					"Unfinished literal string");
			}
			if (hexNum > 0) {
				int d = HexVal(c);
				if (d < 0) {
					throw new Exception(String.Format(
						"not an hex digit: U+{0:X4}",
						c));
				}
				acc = (acc << 4) + d;
				if (-- hexNum == 0) {
					sb.Append((char)acc);
					acc = 0;
				}
			} else if (lcwb) {
				lcwb = false;
				switch (c) {
				case '\n': SkipNL(); break;
				case 'x':
					hexNum = 2;
					break;
				case 'u':
					hexNum = 4;
					break;
				default:
					sb.Append(SingleCharEscape(c));
					break;
				}
			} else {
				switch (c) {
				case '"':
					return sb.ToString();
				case '\\':
					lcwb = true;
					break;
				default:
					sb.Append((char)c);
					break;
				}
			}
		}
	}

	static char SingleCharEscape(int c)
	{
		switch (c) {
		case 'n': return '\n';
		case 'r': return '\r';
		case 't': return '\t';
		case 's': return ' ';
		default:
			return (char)c;
		}
	}

	/*
	 * A backslash+newline sequence occurred in a literal string; we
	 * check and consume the newline escape sequence (whitespace at
	 * start of next line, then a double-quote character).
	 */
	void SkipNL()
	{
		for (;;) {
			int c = NextChar();
			if (c < 0) {
				throw new Exception("EOF in literal string");
			}
			if (c == '\n') {
				throw new Exception(
					"Unescaped newline in literal string");
			}
			if (IsWS(c)) {
				continue;
			}
			if (c == '"') {
				return;
			}
			throw new Exception(
				"Invalid newline escape in literal string");
		}
	}

	static char DecodeCharConst(string t)
	{
		if (t.Length == 1 && t[0] != '\\') {
			return t[0];
		}
		if (t.Length >= 2 && t[0] == '\\') {
			switch (t[1]) {
			case 'x':
				if (t.Length == 4) {
					int x = DecHex(t.Substring(2));
					if (x >= 0) {
						return (char)x;
					}
				}
				break;
			case 'u':
				if (t.Length == 6) {
					int x = DecHex(t.Substring(2));
					if (x >= 0) {
						return (char)x;
					}
				}
				break;
			default:
				if (t.Length == 2) {
					return SingleCharEscape(t[1]);
				}
				break;
			}
		}
		throw new Exception("Invalid literal char: `" + t);
	}

	static int DecHex(string s)
	{
		int acc = 0;
		foreach (char c in s) {
			int d = HexVal(c);
			if (d < 0) {
				return -1;
			}
			acc = (acc << 4) + d;
		}
		return acc;
	}

	static int HexVal(int c)
	{
		if (c >= '0' && c <= '9') {
			return c - '0';
		} else if (c >= 'A' && c <= 'F') {
			return c - ('A' - 10);
		} else if (c >= 'a' && c <= 'f') {
			return c - ('a' - 10);
		} else {
			return -1;
		}
	}

	string ReadTerm(int ct)
	{
		StringBuilder sb = new StringBuilder();
		for (;;) {
			int c = NextChar();
			if (c < 0) {
				throw new Exception(String.Format(
					"EOF reached before U+{0:X4}", ct));
			}
			if (c == ct) {
				return sb.ToString();
			}
			sb.Append((char)c);
		}
	}

	static bool IsWS(int c)
	{
		return c <= 32;
	}

	void ProcessInput(TextReader tr)
	{
		this.currentInput = tr;
		delayedChar = -1;
		Word w = new WordNative(this, "toplevel",
			xcpu => { CompileStep(xcpu); });
		CPU cpu = new CPU();
		Opcode[] code = new Opcode[] {
			new OpcodeCall(w),
			new OpcodeJumpUncond(-2)
		};
		quitRunLoop = false;
		cpu.Enter(code, 0);
		for (;;) {
			if (quitRunLoop) {
				break;
			}
			Opcode op = cpu.ipBuf[cpu.ipOff ++];
			op.Run(cpu);
		}
	}

	void CompileStep(CPU cpu)
	{
		string tt = Next();
		if (tt == null) {
			if (compiling) {
				throw new Exception("EOF while compiling");
			}
			quitRunLoop = true;
			return;
		}
		TValue v;
		bool isVal = TryParseLiteral(tt, out v);
		Word w = LookupNF(tt);
		if (isVal && w != null) {
			throw new Exception(String.Format(
				"Ambiguous: both defined word and literal: {0}",
				tt));
		}
		if (compiling) {
			if (isVal) {
				wordBuilder.Literal(v);
			} else if (w != null) {
				if (w.Immediate) {
					w.Run(cpu);
				} else {
					wordBuilder.CallExt(w);
				}
			} else {
				wordBuilder.Call(tt);
			}
		} else {
			if (isVal) {
				cpu.Push(v);
			} else if (w != null) {
				w.Run(cpu);
			} else {
				throw new Exception(String.Format(
					"Unknown word: '{0}'", tt));
			}
		}
	}

	string GetCCode(string name)
	{
		string ccode;
		allCCode.TryGetValue(name, out ccode);
		return ccode;
	}

	void Generate(string outBase, string coreRun,
		params string[] entryPoints)
	{
		/*
		 * Gather all words that are part of the generated
		 * code. This is done by exploring references
		 * transitively. All such words are thus implicitly
		 * resolved.
		 */
		IDictionary<string, Word> wordSet =
			new SortedDictionary<string, Word>(
				StringComparer.Ordinal);
		Queue<Word> tx = new Queue<Word>();
		foreach (string ep in entryPoints) {
			if (wordSet.ContainsKey(ep)) {
				continue;
			}
			Word w = Lookup(ep);
			wordSet[w.Name] = w;
			tx.Enqueue(w);
		}
		while (tx.Count > 0) {
			Word w = tx.Dequeue();
			foreach (Word w2 in w.GetReferences()) {
				if (wordSet.ContainsKey(w2.Name)) {
					continue;
				}
				wordSet[w2.Name] = w2;
				tx.Enqueue(w2);
			}
		}

		/*
		 * Do flow analysis.
		 */
		if (enableFlowAnalysis) {
			foreach (string ep in entryPoints) {
				Word w = wordSet[ep];
				w.AnalyseFlow();
				Console.WriteLine("{0}: ds={1} rs={2}",
					ep, w.MaxDataStack, w.MaxReturnStack);
				if (w.MaxDataStack > dsLimit) {
					throw new Exception("'" + ep
						+ "' exceeds data stack limit");
				}
				if (w.MaxReturnStack > rsLimit) {
					throw new Exception("'" + ep
						+ "' exceeds return stack"
						+ " limit");
				}
			}
		}

		/*
		 * Gather referenced data areas and compute their
		 * addresses in the generated data block. The address
		 * 0 in the data block is unaffected so that no
		 * valid runtime pointer is equal to null.
		 */
		IDictionary<long, ConstData> blocks =
			new SortedDictionary<long, ConstData>();
		foreach (Word w in wordSet.Values) {
			foreach (ConstData cd in w.GetDataBlocks()) {
				blocks[cd.ID] = cd;
			}
		}
		int dataLen = 1;
		foreach (ConstData cd in blocks.Values) {
			cd.Address = dataLen;
			dataLen += cd.Length;
		}

		/*
		 * Generated code is a sequence of "slot numbers", each
		 * referencing either a piece of explicit C code, or an
		 * entry in the table of interpreted words.
		 *
		 * Opcodes other than "call" get the slots 0 to 6:
		 *
		 *   0   ret           no argument
		 *   1   const         signed value
		 *   2   get local     local number
		 *   3   put local     local number
		 *   4   jump          signed offset
		 *   5   jump if       signed offset
		 *   6   jump if not   signed offset
		 *
		 * The argument, if any, is in "7E" format: the value is
		 * encoded in 7-bit chunk, with big-endian signed
		 * convention. Each 7-bit chunk is encoded over one byte;
		 * the upper bit is 1 for all chunks except the last one.
		 *
		 * Words with explicit C code get the slot numbers
		 * immediately after 6. Interpreted words come afterwards.
		 */
		IDictionary<string, int> slots = new Dictionary<string, int>();
		int curSlot = 7;

		/*
		 * Get explicit C code for words which have such code.
		 * We use string equality on C code so that words with
		 * identical implementations get merged.
		 *
		 * We also check that words with no explicit C code are
		 * interpreted.
		 */
		IDictionary<string, int> ccodeUni =
			new Dictionary<string, int>();
		IDictionary<int, string> ccodeNames =
			new Dictionary<int, string>();
		foreach (Word w in wordSet.Values) {
			string ccode = GetCCode(w.Name);
			if (ccode == null) {
				if (w is WordNative) {
					throw new Exception(String.Format(
						"No C code for native '{0}'",
						w.Name));
				}
				continue;
			}
			int sn;
			if (ccodeUni.ContainsKey(ccode)) {
				sn = ccodeUni[ccode];
				ccodeNames[sn] += " " + EscapeCComment(w.Name);
			} else {
				sn = curSlot ++;
				ccodeUni[ccode] = sn;
				ccodeNames[sn] = EscapeCComment(w.Name);
			}
			slots[w.Name] = sn;
			w.Slot = sn;
		}

		/*
		 * Assign slot values to all remaining words; we know they
		 * are all interpreted.
		 */
		int slotInterpreted = curSlot;
		foreach (Word w in wordSet.Values) {
			if (GetCCode(w.Name) != null) {
				continue;
			}
			int sn = curSlot ++;
			slots[w.Name] = sn;
			w.Slot = sn;
		}
		int numInterpreted = curSlot - slotInterpreted;

		/*
		 * Verify that all entry points are interpreted words.
		 */
		foreach (string ep in entryPoints) {
			if (GetCCode(ep) != null) {
				throw new Exception(
					"Non-interpreted entry point");
			}
		}

		/*
		 * Compute the code block. Each word (without any C code)
		 * yields some CodeElement instances.
		 */
		List<CodeElement> gcodeList = new List<CodeElement>();
		CodeElement[] interpretedEntry =
			new CodeElement[numInterpreted];
		foreach (Word w in wordSet.Values) {
			if (GetCCode(w.Name) != null) {
				continue;
			}
			int n = gcodeList.Count;
			w.GenerateCodeElements(gcodeList);
			interpretedEntry[w.Slot - slotInterpreted] =
				gcodeList[n];
		}
		CodeElement[] gcode = gcodeList.ToArray();

		/*
		 * If there are less than 256 words in total (C +
		 * interpreted) then we can use "one-byte code" which is
		 * more compact when the number of words is in the
		 * 128..255 range.
		 */
		bool oneByteCode;
		if (slotInterpreted + numInterpreted >= 256) {
			Console.WriteLine("WARNING: more than 255 words");
			oneByteCode = false;
		} else {
			oneByteCode = true;
		}

		/*
		 * Compute all addresses and offsets. This loops until
		 * the addresses stabilize.
		 */
		int totalLen = -1;
		int[] gcodeLen = new int[gcode.Length];
		for (;;) {
			for (int i = 0; i < gcode.Length; i ++) {
				gcodeLen[i] = gcode[i].GetLength(oneByteCode);
			}
			int off = 0;
			for (int i = 0; i < gcode.Length; i ++) {
				gcode[i].Address = off;
				gcode[i].LastLength = gcodeLen[i];
				off += gcodeLen[i];
			}
			if (off == totalLen) {
				break;
			}
			totalLen = off;
		}

		/*
		 * Produce output file.
		 */
		using (TextWriter tw = File.CreateText(outBase + ".c")) {
			tw.NewLine = "\n";

			tw.WriteLine("{0}",
@"/* Automatically generated code; do not modify directly. */

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t *dp;
	uint32_t *rp;
	const unsigned char *ip;
} t0_context;

static uint32_t
t0_parse7E_unsigned(const unsigned char **p)
{
	uint32_t x;

	x = 0;
	for (;;) {
		unsigned y;

		y = *(*p) ++;
		x = (x << 7) | (uint32_t)(y & 0x7F);
		if (y < 0x80) {
			return x;
		}
	}
}

static int32_t
t0_parse7E_signed(const unsigned char **p)
{
	int neg;
	uint32_t x;

	neg = ((**p) >> 6) & 1;
	x = (uint32_t)-neg;
	for (;;) {
		unsigned y;

		y = *(*p) ++;
		x = (x << 7) | (uint32_t)(y & 0x7F);
		if (y < 0x80) {
			if (neg) {
				return -(int32_t)~x - 1;
			} else {
				return (int32_t)x;
			}
		}
	}
}

#define T0_VBYTE(x, n)   (unsigned char)((((uint32_t)(x) >> (n)) & 0x7F) | 0x80)
#define T0_FBYTE(x, n)   (unsigned char)(((uint32_t)(x) >> (n)) & 0x7F)
#define T0_SBYTE(x)      (unsigned char)((((uint32_t)(x) >> 28) + 0xF8) ^ 0xF8)
#define T0_INT1(x)       T0_FBYTE(x, 0)
#define T0_INT2(x)       T0_VBYTE(x, 7), T0_FBYTE(x, 0)
#define T0_INT3(x)       T0_VBYTE(x, 14), T0_VBYTE(x, 7), T0_FBYTE(x, 0)
#define T0_INT4(x)       T0_VBYTE(x, 21), T0_VBYTE(x, 14), T0_VBYTE(x, 7), T0_FBYTE(x, 0)
#define T0_INT5(x)       T0_SBYTE(x), T0_VBYTE(x, 21), T0_VBYTE(x, 14), T0_VBYTE(x, 7), T0_FBYTE(x, 0)

/* static const unsigned char t0_datablock[]; */
");

			/*
			 * Add declarations (not definitions) for the
			 * entry point initialisation functions, and the
			 * runner.
			 */
			tw.WriteLine();
			foreach (string ep in entryPoints) {
				tw.WriteLine("void {0}_init_{1}(void *t0ctx);",
					coreRun, ep);
			}
			tw.WriteLine();
			tw.WriteLine("void {0}_run(void *t0ctx);", coreRun);

			/*
			 * Add preamble elements here. They may be needed
			 * for evaluating constant expressions in the
			 * code block.
			 */
			foreach (string pp in extraCode) {
				tw.WriteLine();
				tw.WriteLine("{0}", pp);
			}

			BlobWriter bw;
			tw.WriteLine();
			tw.Write("static const unsigned char"
				+ " t0_datablock[] = {");
			bw = new BlobWriter(tw, 78, 1);
			bw.Append((byte)0);
			foreach (ConstData cd in blocks.Values) {
				cd.Encode(bw);
			}
			tw.WriteLine();
			tw.WriteLine("};");

			tw.WriteLine();
			tw.Write("static const unsigned char"
				+ " t0_codeblock[] = {");
			bw = new BlobWriter(tw, 78, 1);
			foreach (CodeElement ce in gcode) {
				ce.Encode(bw, oneByteCode);
			}
			tw.WriteLine();
			tw.WriteLine("};");

			tw.WriteLine();
			tw.Write("static const uint16_t t0_caddr[] = {");
			for (int i = 0; i < interpretedEntry.Length; i ++) {
				if (i != 0) {
					tw.Write(',');
				}
				tw.WriteLine();
				tw.Write("\t{0}", interpretedEntry[i].Address);
			}
			tw.WriteLine();
			tw.WriteLine("};");

			tw.WriteLine();
			tw.WriteLine("#define T0_INTERPRETED   {0}",
				slotInterpreted);
			tw.WriteLine();
			tw.WriteLine("{0}",
@"#define T0_ENTER(ip, rp, slot)   do { \
		const unsigned char *t0_newip; \
		uint32_t t0_lnum; \
		t0_newip = &t0_codeblock[t0_caddr[(slot) - T0_INTERPRETED]]; \
		t0_lnum = t0_parse7E_unsigned(&t0_newip); \
		(rp) += t0_lnum; \
		*((rp) ++) = (uint32_t)((ip) - &t0_codeblock[0]) + (t0_lnum << 16); \
		(ip) = t0_newip; \
	} while (0)");
			tw.WriteLine();
			tw.WriteLine("{0}",
@"#define T0_DEFENTRY(name, slot) \
void \
name(void *ctx) \
{ \
	t0_context *t0ctx = ctx; \
	t0ctx->ip = &t0_codeblock[0]; \
	T0_ENTER(t0ctx->ip, t0ctx->rp, slot); \
}");

			tw.WriteLine();
			foreach (string ep in entryPoints) {
				tw.WriteLine("T0_DEFENTRY({0}, {1})",
					coreRun + "_init_" + ep,
					wordSet[ep].Slot);
			}
			tw.WriteLine();
			if (oneByteCode) {
				tw.WriteLine("{0}",
@"#define T0_NEXT(t0ipp)   (*(*(t0ipp)) ++)");
			} else {
				tw.WriteLine("{0}",
@"#define T0_NEXT(t0ipp)   t0_parse7E_unsigned(t0ipp)");
			}
			tw.WriteLine();
			tw.WriteLine("void");
			tw.WriteLine("{0}_run(void *t0ctx)", coreRun);
			tw.WriteLine("{0}",
@"{
	uint32_t *dp, *rp;
	const unsigned char *ip;

#define T0_LOCAL(x)    (*(rp - 2 - (x)))
#define T0_POP()       (*-- dp)
#define T0_POPi()      (*(int32_t *)(-- dp))
#define T0_PEEK(x)     (*(dp - 1 - (x)))
#define T0_PEEKi(x)    (*(int32_t *)(dp - 1 - (x)))
#define T0_PUSH(v)     do { *dp = (v); dp ++; } while (0)
#define T0_PUSHi(v)    do { *(int32_t *)dp = (v); dp ++; } while (0)
#define T0_RPOP()      (*-- rp)
#define T0_RPOPi()     (*(int32_t *)(-- rp))
#define T0_RPUSH(v)    do { *rp = (v); rp ++; } while (0)
#define T0_RPUSHi(v)   do { *(int32_t *)rp = (v); rp ++; } while (0)
#define T0_ROLL(x)     do { \
	size_t t0len = (size_t)(x); \
	uint32_t t0tmp = *(dp - 1 - t0len); \
	memmove(dp - t0len - 1, dp - t0len, t0len * sizeof *dp); \
	*(dp - 1) = t0tmp; \
} while (0)
#define T0_SWAP()      do { \
	uint32_t t0tmp = *(dp - 2); \
	*(dp - 2) = *(dp - 1); \
	*(dp - 1) = t0tmp; \
} while (0)
#define T0_ROT()       do { \
	uint32_t t0tmp = *(dp - 3); \
	*(dp - 3) = *(dp - 2); \
	*(dp - 2) = *(dp - 1); \
	*(dp - 1) = t0tmp; \
} while (0)
#define T0_NROT()       do { \
	uint32_t t0tmp = *(dp - 1); \
	*(dp - 1) = *(dp - 2); \
	*(dp - 2) = *(dp - 3); \
	*(dp - 3) = t0tmp; \
} while (0)
#define T0_PICK(x)      do { \
	uint32_t t0depth = (x); \
	T0_PUSH(T0_PEEK(t0depth)); \
} while (0)
#define T0_CO()         do { \
	goto t0_exit; \
} while (0)
#define T0_RET()        goto t0_next

	dp = ((t0_context *)t0ctx)->dp;
	rp = ((t0_context *)t0ctx)->rp;
	ip = ((t0_context *)t0ctx)->ip;
	goto t0_next;
	for (;;) {
		uint32_t t0x;

	t0_next:
		t0x = T0_NEXT(&ip);
		if (t0x < T0_INTERPRETED) {
			switch (t0x) {
				int32_t t0off;

			case 0: /* ret */
				t0x = T0_RPOP();
				rp -= (t0x >> 16);
				t0x &= 0xFFFF;
				if (t0x == 0) {
					ip = NULL;
					goto t0_exit;
				}
				ip = &t0_codeblock[t0x];
				break;
			case 1: /* literal constant */
				T0_PUSHi(t0_parse7E_signed(&ip));
				break;
			case 2: /* read local */
				T0_PUSH(T0_LOCAL(t0_parse7E_unsigned(&ip)));
				break;
			case 3: /* write local */
				T0_LOCAL(t0_parse7E_unsigned(&ip)) = T0_POP();
				break;
			case 4: /* jump */
				t0off = t0_parse7E_signed(&ip);
				ip += t0off;
				break;
			case 5: /* jump if */
				t0off = t0_parse7E_signed(&ip);
				if (T0_POP()) {
					ip += t0off;
				}
				break;
			case 6: /* jump if not */
				t0off = t0_parse7E_signed(&ip);
				if (!T0_POP()) {
					ip += t0off;
				}
				break;");

			SortedDictionary<int, string> nccode =
				new SortedDictionary<int, string>();
			foreach (string k in ccodeUni.Keys) {
				nccode[ccodeUni[k]] = k;
			}
			foreach (int sn in nccode.Keys) {
				tw.WriteLine(
@"			case {0}: {{
				/* {1} */
{2}
				}}
				break;", sn, ccodeNames[sn], nccode[sn]);
			}

			tw.WriteLine(
@"			}

		} else {
			T0_ENTER(ip, rp, t0x);
		}
	}
t0_exit:
	((t0_context *)t0ctx)->dp = dp;
	((t0_context *)t0ctx)->rp = rp;
	((t0_context *)t0ctx)->ip = ip;
}");

			/*
			 * Add the "postamblr" elements here. These are
			 * elements that may need access to the data
			 * block or code block, so they must occur after
			 * their definition.
			 */
			foreach (string pp in extraCodeDefer) {
				tw.WriteLine();
				tw.WriteLine("{0}", pp);
			}
		}

		int codeLen = 0;
		foreach (CodeElement ce in gcode) {
			codeLen += ce.GetLength(oneByteCode);
		}
		int dataBlockLen = 0;
		foreach (ConstData cd in blocks.Values) {
			dataBlockLen += cd.Length;
		}

		/*
		 * Write some statistics on produced code.
		 */
		Console.WriteLine("code length: {0,6} byte(s)", codeLen);
		Console.WriteLine("data length: {0,6} byte(s)", dataLen);
		Console.WriteLine("total words: {0} (interpreted: {1})",
			slotInterpreted + numInterpreted, numInterpreted);
	}

	internal Word Lookup(string name)
	{
		Word w = LookupNF(name);
		if (w != null) {
			return w;
		}
		throw new Exception(String.Format("No such word: '{0}'", name));
	}

	internal Word LookupNF(string name)
	{
		Word w;
		words.TryGetValue(name, out w);
		return w;
	}

	internal TValue StringToBlob(string s)
	{
		return new TValue(0, new TPointerBlob(this, s));
	}

	internal bool TryParseLiteral(string tt, out TValue tv)
	{
		tv = new TValue(0);
		if (tt.StartsWith("\"")) {
			tv = StringToBlob(tt.Substring(1));
			return true;
		}
		if (tt.StartsWith("`")) {
			tv = DecodeCharConst(tt.Substring(1));
			return true;
		}
		bool neg = false;
		if (tt.StartsWith("-")) {
			neg = true;
			tt = tt.Substring(1);
		} else if (tt.StartsWith("+")) {
			tt = tt.Substring(1);
		}
		uint radix = 10;
		if (tt.StartsWith("0x") || tt.StartsWith("0X")) {
			radix = 16;
			tt = tt.Substring(2);
		} else if (tt.StartsWith("0b") || tt.StartsWith("0B")) {
			radix = 2;
			tt = tt.Substring(2);
		}
		if (tt.Length == 0) {
			return false;
		}
		uint acc = 0;
		bool overflow = false;
		uint maxV = uint.MaxValue / radix;
		foreach (char c in tt) {
			int d = HexVal(c);
			if (d < 0 || d >= radix) {
				return false;
			}
			if (acc > maxV) {
				overflow = true;
			}
			acc *= radix;
			if ((uint)d > uint.MaxValue - acc) {
				overflow = true;
			}
			acc += (uint)d;
		}
		int x = (int)acc;
		if (neg) {
			if (acc > (uint)0x80000000) {
				overflow = true;
			}
			x = -x;
		}
		if (overflow) {
			throw new Exception(
				"invalid literal integer (overflow)");
		}
		tv = x;
		return true;
	}

	int ParseInteger(string tt)
	{
		TValue tv;
		if (!TryParseLiteral(tt, out tv)) {
			throw new Exception("not an integer: " + ToString());
		}
		return (int)tv;
	}

	void CheckCompiling()
	{
		if (!compiling) {
			throw new Exception("Not in compilation mode");
		}
	}

	static string EscapeCComment(string s)
	{
		StringBuilder sb = new StringBuilder();
		foreach (char c in s) {
			if (c >= 33 && c <= 126 && c != '%') {
				sb.Append(c);
			} else if (c < 0x100) {
				sb.AppendFormat("%{0:X2}", (int)c);
			} else if (c < 0x800) {
				sb.AppendFormat("%{0:X2}%{0:X2}",
					((int)c >> 6) | 0xC0,
					((int)c & 0x3F) | 0x80);
			} else {
				sb.AppendFormat("%{0:X2}%{0:X2}%{0:X2}",
					((int)c >> 12) | 0xE0,
					(((int)c >> 6) & 0x3F) | 0x80,
					((int)c & 0x3F) | 0x80);
			}
		}
		return sb.ToString().Replace("*/", "%2A/");
	}
}
