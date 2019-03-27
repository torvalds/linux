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
using System.IO;
using System.Text;

/*
 * A simple class for writing out bytes as hexadecimal constants or
 * explicit expressions for the initializer of a C array of 'unsigned
 * char'. It starts every line with a given number of tabs, and aims at
 * keeping lines below a given threshold (each indentation tab counts as
 * 8 characters). An explicit newline is inserted before the first
 * element, and commas are used as separators.
 */

class BlobWriter {

	TextWriter w;
	int maxLineLen;
	int indent;
	int lineLen;

	/*
	 * Create a new instance. 'maxLineLen' is in characters, and
	 * 'indent' is the number of tab characters at the start of
	 * each line.
	 */
	internal BlobWriter(TextWriter w, int maxLineLen, int indent)
	{
		this.w = w;
		this.maxLineLen = maxLineLen;
		this.indent = indent;
		lineLen = -1;
	}

	void DoNL()
	{
		w.WriteLine();
		for (int i = 0; i < indent; i ++) {
			w.Write('\t');
		}
		lineLen = (indent << 3);
	}

	/*
	 * Append a new byte value; it will be converted to an hexadecimal
	 * constant in the output.
	 */
	internal void Append(byte b)
	{
		if (lineLen < 0) {
			DoNL();
		} else {
			w.Write(',');
			lineLen ++;
			if ((lineLen + 5) > maxLineLen) {
				DoNL();
			} else {
				w.Write(' ');
				lineLen ++;
			}
		}
		w.Write("0x{0:X2}", b);
		lineLen += 4;
	}

	/*
	 * Append a C expression, which will be used as is. The expression
	 * may resolve to several bytes if it uses internal commas. The
	 * writer will try to honour the expected line length, but it
	 * won't insert a newline character inside the expression.
	 */
	internal void Append(string expr)
	{
		if (lineLen < 0) {
			DoNL();
		} else {
			w.Write(',');
			lineLen ++;
			if ((lineLen + 1 + expr.Length) > maxLineLen) {
				DoNL();
			} else {
				w.Write(' ');
				lineLen ++;
			}
		}
		w.Write("{0}", expr);
		lineLen += expr.Length;
	}
}
