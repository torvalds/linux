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

class WordData : Word {

	ConstData blob;
	string baseBlobName;
	int offset;
	bool ongoingResolution;

	internal WordData(T0Comp owner, string name,
		ConstData blob, int offset)
		: base(owner, name)
	{
		this.blob = blob;
		this.offset = offset;
		StackEffect = new SType(0, 1);
	}

	internal WordData(T0Comp owner, string name,
		string baseBlobName, int offset)
		: base(owner, name)
	{
		this.baseBlobName = baseBlobName;
		this.offset = offset;
		StackEffect = new SType(0, 1);
	}

	internal override void Resolve()
	{
		if (blob != null) {
			return;
		}
		if (ongoingResolution) {
			throw new Exception(String.Format(
				"circular reference in blobs ({0})", Name));
		}
		ongoingResolution = true;
		WordData wd = TC.Lookup(baseBlobName) as WordData;
		if (wd == null) {
			throw new Exception(String.Format(
				"data word '{0}' based on non-data word '{1}'",
				Name, baseBlobName));
		}
		wd.Resolve();
		blob = wd.blob;
		offset += wd.offset;
		ongoingResolution = false;
	}

	internal override void Run(CPU cpu)
	{
		Resolve();
		cpu.Push(new TValue(offset, new TPointerBlob(blob)));
	}

	internal override List<ConstData> GetDataBlocks()
	{
		Resolve();
		List<ConstData> r = new List<ConstData>();
		r.Add(blob);
		return r;
	}

	internal override void GenerateCodeElements(List<CodeElement> dst)
	{
		Resolve();
		dst.Add(new CodeElementUInt(0));
		dst.Add(new CodeElementUIntInt(1, blob.Address + offset));
		dst.Add(new CodeElementUInt(0));
	}
}
