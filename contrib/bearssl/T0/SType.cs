using System;

/*
 * This structure contains the stack effect of a word: number of stack
 * element consumed on input, and number of stack element produced on
 * output.
 */

struct SType {

	/*
	 * Get number of stack elements consumed on input; this is -1 if
	 * the stack effect is not known.
	 */
	internal int DataIn {
		get {
			return din;
		}
	}

	/*
	 * Get number of stack elements produced on output; this is -1 if
	 * either the stack effect is not known, or if the word never
	 * exits.
	 */
	internal int DataOut {
		get {
			return dout;
		}
	}

	/*
	 * Tell whether the stack effect is known.
	 */
	internal bool IsKnown {
		get {
			return din >= 0;
		}
	}

	/*
	 * Tell whether the stack effect is known and the word never exits.
	 */
	internal bool NoExit {
		get {
			return din >= 0 && dout < 0;
		}
	}

	int din, dout;

	internal SType(int din, int dout)
	{
		if (din < 0) {
			din = -1;
		}
		if (dout < 0) {
			dout = -1;
		}
		this.din = din;
		this.dout = dout;
	}

	/*
	 * Special value for the unknown stack effect.
	 */
	internal static SType UNKNOWN = new SType(-1, -1);

	/*
	 * Constant for the "blank stack effect".
	 */
	internal static SType BLANK = new SType(0, 0);

	public static bool operator ==(SType s1, SType s2)
	{
		return s1.din == s2.din && s1.dout == s2.dout;
	}

	public static bool operator !=(SType s1, SType s2)
	{
		return s1.din != s2.din || s1.dout != s2.dout;
	}

	public override bool Equals(Object obj)
	{
		return (obj is SType) && ((SType)obj == this);
	}

	public override int GetHashCode()
	{
		return din * 31 + dout * 17;
	}

	public override string ToString()
	{
		if (!IsKnown) {
			return "UNKNOWN";
		} else if (NoExit) {
			return string.Format("in:{0},noexit", din);
		} else {
			return string.Format("in:{0},out:{1}", din, dout);
		}
	}

	/*
	 * Test whether this stack effect is a sub-effect of the provided
	 * stack effect s. Stack effect s1 is a sub-effect of stack-effect
	 * s2 if any of the following holds:
	 * -- s1 and s2 are known, s1.din <= s2.din and s1 does not exit.
	 * -- s1 and s2 are known, s1.din <= s2.din, s1 and s2 exit,
	 *    and s1.din - s1.dout == s2.din - s2.dout.
	 */
	internal bool IsSubOf(SType s)
	{
		if (!IsKnown || !s.IsKnown) {
			return false;
		}
		if (din > s.din) {
			return false;
		}
		if (NoExit) {
			return true;
		}
		if (s.NoExit) {
			return false;
		}
		return (din - dout) == (s.din - s.dout);
	}
}
