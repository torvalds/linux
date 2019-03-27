#!/usr/perl5/bin/perl

BEGIN {
	if (index("foobarbaz", "barbaz") != 3) {
		printf("perl => index(\"foobarbaz\", \"barbaz\") = %d\n",
		    index("foobarbaz", "barbaz"));
		printf("   D => index(\"foobarbaz\", \"barbaz\") = 3\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz") != 3) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\") = %d\n",
		    rindex("foobarbaz", "barbaz"));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\") = 3\n");
		$failed++;
	}

	if (index("foofoofoo", "foo") != 0) {
		printf("perl => index(\"foofoofoo\", \"foo\") = %d\n",
		    index("foofoofoo", "foo"));
		printf("   D => index(\"foofoofoo\", \"foo\") = 0\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo") != 6) {
		printf("perl => rindex(\"foofoofoo\", \"foo\") = %d\n",
		    rindex("foofoofoo", "foo"));
		printf("   D => rindex(\"foofoofoo\", \"foo\") = 6\n");
		$failed++;
	}

	if (index("boofoofoo", "foo") != 3) {
		printf("perl => index(\"boofoofoo\", \"foo\") = %d\n",
		    index("boofoofoo", "foo"));
		printf("   D => index(\"boofoofoo\", \"foo\") = 3\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo") != 6) {
		printf("perl => rindex(\"boofoofoo\", \"foo\") = %d\n",
		    rindex("boofoofoo", "foo"));
		printf("   D => rindex(\"boofoofoo\", \"foo\") = 6\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy") != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\") = %d\n",
		    index("foobarbaz", "barbazzy"));
		printf("   D => index(\"foobarbaz\", \"barbazzy\") = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy") != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\") = %d\n",
		    rindex("foobarbaz", "barbazzy"));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\") = -1\n");
		$failed++;
	}

	if (index("foobar", "foobar") != 0) {
		printf("perl => index(\"foobar\", \"foobar\") = %d\n",
		    index("foobar", "foobar"));
		printf("   D => index(\"foobar\", \"foobar\") = 0\n");
		$failed++;
	}

	if (rindex("foobar", "foobar") != 0) {
		printf("perl => rindex(\"foobar\", \"foobar\") = %d\n",
		    rindex("foobar", "foobar"));
		printf("   D => rindex(\"foobar\", \"foobar\") = 0\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz") != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\") = %d\n",
		    index("foobar", "foobarbaz"));
		printf("   D => index(\"foobar\", \"foobarbaz\") = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz") != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\") = %d\n",
		    rindex("foobar", "foobarbaz"));
		printf("   D => rindex(\"foobar\", \"foobarbaz\") = -1\n");
		$failed++;
	}

	if (index("", "foobar") != -1) {
		printf("perl => index(\"\", \"foobar\") = %d\n",
		    index("", "foobar"));
		printf("   D => index(\"\", \"foobar\") = -1\n");
		$failed++;
	}

	if (rindex("", "foobar") != -1) {
		printf("perl => rindex(\"\", \"foobar\") = %d\n",
		    rindex("", "foobar"));
		printf("   D => rindex(\"\", \"foobar\") = -1\n");
		$failed++;
	}

	if (index("foobar", "") != 0) {
		printf("perl => index(\"foobar\", \"\") = %d\n",
		    index("foobar", ""));
		printf("   D => index(\"foobar\", \"\") = 0\n");
		$failed++;
	}

	if (rindex("foobar", "") != 6) {
		printf("perl => rindex(\"foobar\", \"\") = %d\n",
		    rindex("foobar", ""));
		printf("   D => rindex(\"foobar\", \"\") = 6\n");
		$failed++;
	}

	if (index("", "") != 0) {
		printf("perl => index(\"\", \"\") = %d\n",
		    index("", ""));
		printf("   D => index(\"\", \"\") = 0\n");
		$failed++;
	}

	if (rindex("", "") != 0) {
		printf("perl => rindex(\"\", \"\") = %d\n",
		    rindex("", ""));
		printf("   D => rindex(\"\", \"\") = 0\n");
		$failed++;
	}

	if (index("foo", "") != 0) {
		printf("perl => index(\"foo\", \"\") = %d\n",
		    index("foo", ""));
		printf("   D => index(\"foo\", \"\") = 0\n");
		$failed++;
	}

	if (rindex("foo", "") != 3) {
		printf("perl => rindex(\"foo\", \"\") = %d\n",
		    rindex("foo", ""));
		printf("   D => rindex(\"foo\", \"\") = 3\n");
		$failed++;
	}

	if (index("foobarbaz", "barbaz", -400) != 3) {
		printf("perl => index(\"foobarbaz\", \"barbaz\", -400) = %d\n",
		    index("foobarbaz", "barbaz", -400));
		printf("   D => index(\"foobarbaz\", \"barbaz\", -400) = 3\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz", -400) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\", -400) = %d\n",
		    rindex("foobarbaz", "barbaz", -400));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\", -400) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbaz", -1) != 3) {
		printf("perl => index(\"foobarbaz\", \"barbaz\", -1) = %d\n",
		    index("foobarbaz", "barbaz", -1));
		printf("   D => index(\"foobarbaz\", \"barbaz\", -1) = 3\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz", -1) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\", -1) = %d\n",
		    rindex("foobarbaz", "barbaz", -1));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\", -1) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbaz", 0) != 3) {
		printf("perl => index(\"foobarbaz\", \"barbaz\", 0) = %d\n",
		    index("foobarbaz", "barbaz", 0));
		printf("   D => index(\"foobarbaz\", \"barbaz\", 0) = 3\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz", 0) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\", 0) = %d\n",
		    rindex("foobarbaz", "barbaz", 0));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\", 0) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbaz", 4) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbaz\", 4) = %d\n",
		    index("foobarbaz", "barbaz", 4));
		printf("   D => index(\"foobarbaz\", \"barbaz\", 4) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz", 4) != 3) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\", 4) = %d\n",
		    rindex("foobarbaz", "barbaz", 4));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\", 4) = 3\n");
		$failed++;
	}

	if (index("foobarbaz", "barbaz", 9) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbaz\", 9) = %d\n",
		    index("foobarbaz", "barbaz", 9));
		printf("   D => index(\"foobarbaz\", \"barbaz\", 9) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz", 9) != 3) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\", 9) = %d\n",
		    rindex("foobarbaz", "barbaz", 9));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\", 9) = 3\n");
		$failed++;
	}

	if (index("foobarbaz", "barbaz", 10) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbaz\", 10) = %d\n",
		    index("foobarbaz", "barbaz", 10));
		printf("   D => index(\"foobarbaz\", \"barbaz\", 10) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz", 10) != 3) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\", 10) = %d\n",
		    rindex("foobarbaz", "barbaz", 10));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\", 10) = 3\n");
		$failed++;
	}

	if (index("foobarbaz", "barbaz", 11) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbaz\", 11) = %d\n",
		    index("foobarbaz", "barbaz", 11));
		printf("   D => index(\"foobarbaz\", \"barbaz\", 11) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz", 11) != 3) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\", 11) = %d\n",
		    rindex("foobarbaz", "barbaz", 11));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\", 11) = 3\n");
		$failed++;
	}

	if (index("foobarbaz", "barbaz", 400) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbaz\", 400) = %d\n",
		    index("foobarbaz", "barbaz", 400));
		printf("   D => index(\"foobarbaz\", \"barbaz\", 400) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbaz", 400) != 3) {
		printf("perl => rindex(\"foobarbaz\", \"barbaz\", 400) = %d\n",
		    rindex("foobarbaz", "barbaz", 400));
		printf("   D => rindex(\"foobarbaz\", \"barbaz\", 400) = 3\n");
		$failed++;
	}

	if (index("foofoofoo", "foo", -400) != 0) {
		printf("perl => index(\"foofoofoo\", \"foo\", -400) = %d\n",
		    index("foofoofoo", "foo", -400));
		printf("   D => index(\"foofoofoo\", \"foo\", -400) = 0\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo", -400) != -1) {
		printf("perl => rindex(\"foofoofoo\", \"foo\", -400) = %d\n",
		    rindex("foofoofoo", "foo", -400));
		printf("   D => rindex(\"foofoofoo\", \"foo\", -400) = -1\n");
		$failed++;
	}

	if (index("foofoofoo", "foo", -1) != 0) {
		printf("perl => index(\"foofoofoo\", \"foo\", -1) = %d\n",
		    index("foofoofoo", "foo", -1));
		printf("   D => index(\"foofoofoo\", \"foo\", -1) = 0\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo", -1) != -1) {
		printf("perl => rindex(\"foofoofoo\", \"foo\", -1) = %d\n",
		    rindex("foofoofoo", "foo", -1));
		printf("   D => rindex(\"foofoofoo\", \"foo\", -1) = -1\n");
		$failed++;
	}

	if (index("foofoofoo", "foo", 0) != 0) {
		printf("perl => index(\"foofoofoo\", \"foo\", 0) = %d\n",
		    index("foofoofoo", "foo", 0));
		printf("   D => index(\"foofoofoo\", \"foo\", 0) = 0\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo", 0) != 0) {
		printf("perl => rindex(\"foofoofoo\", \"foo\", 0) = %d\n",
		    rindex("foofoofoo", "foo", 0));
		printf("   D => rindex(\"foofoofoo\", \"foo\", 0) = 0\n");
		$failed++;
	}

	if (index("foofoofoo", "foo", 4) != 6) {
		printf("perl => index(\"foofoofoo\", \"foo\", 4) = %d\n",
		    index("foofoofoo", "foo", 4));
		printf("   D => index(\"foofoofoo\", \"foo\", 4) = 6\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo", 4) != 3) {
		printf("perl => rindex(\"foofoofoo\", \"foo\", 4) = %d\n",
		    rindex("foofoofoo", "foo", 4));
		printf("   D => rindex(\"foofoofoo\", \"foo\", 4) = 3\n");
		$failed++;
	}

	if (index("foofoofoo", "foo", 9) != -1) {
		printf("perl => index(\"foofoofoo\", \"foo\", 9) = %d\n",
		    index("foofoofoo", "foo", 9));
		printf("   D => index(\"foofoofoo\", \"foo\", 9) = -1\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo", 9) != 6) {
		printf("perl => rindex(\"foofoofoo\", \"foo\", 9) = %d\n",
		    rindex("foofoofoo", "foo", 9));
		printf("   D => rindex(\"foofoofoo\", \"foo\", 9) = 6\n");
		$failed++;
	}

	if (index("foofoofoo", "foo", 10) != -1) {
		printf("perl => index(\"foofoofoo\", \"foo\", 10) = %d\n",
		    index("foofoofoo", "foo", 10));
		printf("   D => index(\"foofoofoo\", \"foo\", 10) = -1\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo", 10) != 6) {
		printf("perl => rindex(\"foofoofoo\", \"foo\", 10) = %d\n",
		    rindex("foofoofoo", "foo", 10));
		printf("   D => rindex(\"foofoofoo\", \"foo\", 10) = 6\n");
		$failed++;
	}

	if (index("foofoofoo", "foo", 11) != -1) {
		printf("perl => index(\"foofoofoo\", \"foo\", 11) = %d\n",
		    index("foofoofoo", "foo", 11));
		printf("   D => index(\"foofoofoo\", \"foo\", 11) = -1\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo", 11) != 6) {
		printf("perl => rindex(\"foofoofoo\", \"foo\", 11) = %d\n",
		    rindex("foofoofoo", "foo", 11));
		printf("   D => rindex(\"foofoofoo\", \"foo\", 11) = 6\n");
		$failed++;
	}

	if (index("foofoofoo", "foo", 400) != -1) {
		printf("perl => index(\"foofoofoo\", \"foo\", 400) = %d\n",
		    index("foofoofoo", "foo", 400));
		printf("   D => index(\"foofoofoo\", \"foo\", 400) = -1\n");
		$failed++;
	}

	if (rindex("foofoofoo", "foo", 400) != 6) {
		printf("perl => rindex(\"foofoofoo\", \"foo\", 400) = %d\n",
		    rindex("foofoofoo", "foo", 400));
		printf("   D => rindex(\"foofoofoo\", \"foo\", 400) = 6\n");
		$failed++;
	}

	if (index("boofoofoo", "foo", -400) != 3) {
		printf("perl => index(\"boofoofoo\", \"foo\", -400) = %d\n",
		    index("boofoofoo", "foo", -400));
		printf("   D => index(\"boofoofoo\", \"foo\", -400) = 3\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo", -400) != -1) {
		printf("perl => rindex(\"boofoofoo\", \"foo\", -400) = %d\n",
		    rindex("boofoofoo", "foo", -400));
		printf("   D => rindex(\"boofoofoo\", \"foo\", -400) = -1\n");
		$failed++;
	}

	if (index("boofoofoo", "foo", -1) != 3) {
		printf("perl => index(\"boofoofoo\", \"foo\", -1) = %d\n",
		    index("boofoofoo", "foo", -1));
		printf("   D => index(\"boofoofoo\", \"foo\", -1) = 3\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo", -1) != -1) {
		printf("perl => rindex(\"boofoofoo\", \"foo\", -1) = %d\n",
		    rindex("boofoofoo", "foo", -1));
		printf("   D => rindex(\"boofoofoo\", \"foo\", -1) = -1\n");
		$failed++;
	}

	if (index("boofoofoo", "foo", 0) != 3) {
		printf("perl => index(\"boofoofoo\", \"foo\", 0) = %d\n",
		    index("boofoofoo", "foo", 0));
		printf("   D => index(\"boofoofoo\", \"foo\", 0) = 3\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo", 0) != -1) {
		printf("perl => rindex(\"boofoofoo\", \"foo\", 0) = %d\n",
		    rindex("boofoofoo", "foo", 0));
		printf("   D => rindex(\"boofoofoo\", \"foo\", 0) = -1\n");
		$failed++;
	}

	if (index("boofoofoo", "foo", 4) != 6) {
		printf("perl => index(\"boofoofoo\", \"foo\", 4) = %d\n",
		    index("boofoofoo", "foo", 4));
		printf("   D => index(\"boofoofoo\", \"foo\", 4) = 6\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo", 4) != 3) {
		printf("perl => rindex(\"boofoofoo\", \"foo\", 4) = %d\n",
		    rindex("boofoofoo", "foo", 4));
		printf("   D => rindex(\"boofoofoo\", \"foo\", 4) = 3\n");
		$failed++;
	}

	if (index("boofoofoo", "foo", 9) != -1) {
		printf("perl => index(\"boofoofoo\", \"foo\", 9) = %d\n",
		    index("boofoofoo", "foo", 9));
		printf("   D => index(\"boofoofoo\", \"foo\", 9) = -1\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo", 9) != 6) {
		printf("perl => rindex(\"boofoofoo\", \"foo\", 9) = %d\n",
		    rindex("boofoofoo", "foo", 9));
		printf("   D => rindex(\"boofoofoo\", \"foo\", 9) = 6\n");
		$failed++;
	}

	if (index("boofoofoo", "foo", 10) != -1) {
		printf("perl => index(\"boofoofoo\", \"foo\", 10) = %d\n",
		    index("boofoofoo", "foo", 10));
		printf("   D => index(\"boofoofoo\", \"foo\", 10) = -1\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo", 10) != 6) {
		printf("perl => rindex(\"boofoofoo\", \"foo\", 10) = %d\n",
		    rindex("boofoofoo", "foo", 10));
		printf("   D => rindex(\"boofoofoo\", \"foo\", 10) = 6\n");
		$failed++;
	}

	if (index("boofoofoo", "foo", 11) != -1) {
		printf("perl => index(\"boofoofoo\", \"foo\", 11) = %d\n",
		    index("boofoofoo", "foo", 11));
		printf("   D => index(\"boofoofoo\", \"foo\", 11) = -1\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo", 11) != 6) {
		printf("perl => rindex(\"boofoofoo\", \"foo\", 11) = %d\n",
		    rindex("boofoofoo", "foo", 11));
		printf("   D => rindex(\"boofoofoo\", \"foo\", 11) = 6\n");
		$failed++;
	}

	if (index("boofoofoo", "foo", 400) != -1) {
		printf("perl => index(\"boofoofoo\", \"foo\", 400) = %d\n",
		    index("boofoofoo", "foo", 400));
		printf("   D => index(\"boofoofoo\", \"foo\", 400) = -1\n");
		$failed++;
	}

	if (rindex("boofoofoo", "foo", 400) != 6) {
		printf("perl => rindex(\"boofoofoo\", \"foo\", 400) = %d\n",
		    rindex("boofoofoo", "foo", 400));
		printf("   D => rindex(\"boofoofoo\", \"foo\", 400) = 6\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy", -400) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\", -400) = %d\n",
		    index("foobarbaz", "barbazzy", -400));
		printf("   D => index(\"foobarbaz\", \"barbazzy\", -400) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy", -400) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\", -400) = %d\n",
		    rindex("foobarbaz", "barbazzy", -400));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\", -400) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy", -1) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\", -1) = %d\n",
		    index("foobarbaz", "barbazzy", -1));
		printf("   D => index(\"foobarbaz\", \"barbazzy\", -1) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy", -1) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\", -1) = %d\n",
		    rindex("foobarbaz", "barbazzy", -1));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\", -1) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy", 0) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\", 0) = %d\n",
		    index("foobarbaz", "barbazzy", 0));
		printf("   D => index(\"foobarbaz\", \"barbazzy\", 0) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy", 0) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\", 0) = %d\n",
		    rindex("foobarbaz", "barbazzy", 0));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\", 0) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy", 4) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\", 4) = %d\n",
		    index("foobarbaz", "barbazzy", 4));
		printf("   D => index(\"foobarbaz\", \"barbazzy\", 4) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy", 4) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\", 4) = %d\n",
		    rindex("foobarbaz", "barbazzy", 4));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\", 4) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy", 9) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\", 9) = %d\n",
		    index("foobarbaz", "barbazzy", 9));
		printf("   D => index(\"foobarbaz\", \"barbazzy\", 9) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy", 9) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\", 9) = %d\n",
		    rindex("foobarbaz", "barbazzy", 9));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\", 9) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy", 10) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\", 10) = %d\n",
		    index("foobarbaz", "barbazzy", 10));
		printf("   D => index(\"foobarbaz\", \"barbazzy\", 10) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy", 10) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\", 10) = %d\n",
		    rindex("foobarbaz", "barbazzy", 10));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\", 10) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy", 11) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\", 11) = %d\n",
		    index("foobarbaz", "barbazzy", 11));
		printf("   D => index(\"foobarbaz\", \"barbazzy\", 11) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy", 11) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\", 11) = %d\n",
		    rindex("foobarbaz", "barbazzy", 11));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\", 11) = -1\n");
		$failed++;
	}

	if (index("foobarbaz", "barbazzy", 400) != -1) {
		printf("perl => index(\"foobarbaz\", \"barbazzy\", 400) = %d\n",
		    index("foobarbaz", "barbazzy", 400));
		printf("   D => index(\"foobarbaz\", \"barbazzy\", 400) = -1\n");
		$failed++;
	}

	if (rindex("foobarbaz", "barbazzy", 400) != -1) {
		printf("perl => rindex(\"foobarbaz\", \"barbazzy\", 400) = %d\n",
		    rindex("foobarbaz", "barbazzy", 400));
		printf("   D => rindex(\"foobarbaz\", \"barbazzy\", 400) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobar", -400) != 0) {
		printf("perl => index(\"foobar\", \"foobar\", -400) = %d\n",
		    index("foobar", "foobar", -400));
		printf("   D => index(\"foobar\", \"foobar\", -400) = 0\n");
		$failed++;
	}

	if (rindex("foobar", "foobar", -400) != -1) {
		printf("perl => rindex(\"foobar\", \"foobar\", -400) = %d\n",
		    rindex("foobar", "foobar", -400));
		printf("   D => rindex(\"foobar\", \"foobar\", -400) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobar", -1) != 0) {
		printf("perl => index(\"foobar\", \"foobar\", -1) = %d\n",
		    index("foobar", "foobar", -1));
		printf("   D => index(\"foobar\", \"foobar\", -1) = 0\n");
		$failed++;
	}

	if (rindex("foobar", "foobar", -1) != -1) {
		printf("perl => rindex(\"foobar\", \"foobar\", -1) = %d\n",
		    rindex("foobar", "foobar", -1));
		printf("   D => rindex(\"foobar\", \"foobar\", -1) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobar", 0) != 0) {
		printf("perl => index(\"foobar\", \"foobar\", 0) = %d\n",
		    index("foobar", "foobar", 0));
		printf("   D => index(\"foobar\", \"foobar\", 0) = 0\n");
		$failed++;
	}

	if (rindex("foobar", "foobar", 0) != 0) {
		printf("perl => rindex(\"foobar\", \"foobar\", 0) = %d\n",
		    rindex("foobar", "foobar", 0));
		printf("   D => rindex(\"foobar\", \"foobar\", 0) = 0\n");
		$failed++;
	}

	if (index("foobar", "foobar", 3) != -1) {
		printf("perl => index(\"foobar\", \"foobar\", 3) = %d\n",
		    index("foobar", "foobar", 3));
		printf("   D => index(\"foobar\", \"foobar\", 3) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobar", 3) != 0) {
		printf("perl => rindex(\"foobar\", \"foobar\", 3) = %d\n",
		    rindex("foobar", "foobar", 3));
		printf("   D => rindex(\"foobar\", \"foobar\", 3) = 0\n");
		$failed++;
	}

	if (index("foobar", "foobar", 6) != -1) {
		printf("perl => index(\"foobar\", \"foobar\", 6) = %d\n",
		    index("foobar", "foobar", 6));
		printf("   D => index(\"foobar\", \"foobar\", 6) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobar", 6) != 0) {
		printf("perl => rindex(\"foobar\", \"foobar\", 6) = %d\n",
		    rindex("foobar", "foobar", 6));
		printf("   D => rindex(\"foobar\", \"foobar\", 6) = 0\n");
		$failed++;
	}

	if (index("foobar", "foobar", 7) != -1) {
		printf("perl => index(\"foobar\", \"foobar\", 7) = %d\n",
		    index("foobar", "foobar", 7));
		printf("   D => index(\"foobar\", \"foobar\", 7) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobar", 7) != 0) {
		printf("perl => rindex(\"foobar\", \"foobar\", 7) = %d\n",
		    rindex("foobar", "foobar", 7));
		printf("   D => rindex(\"foobar\", \"foobar\", 7) = 0\n");
		$failed++;
	}

	if (index("foobar", "foobar", 8) != -1) {
		printf("perl => index(\"foobar\", \"foobar\", 8) = %d\n",
		    index("foobar", "foobar", 8));
		printf("   D => index(\"foobar\", \"foobar\", 8) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobar", 8) != 0) {
		printf("perl => rindex(\"foobar\", \"foobar\", 8) = %d\n",
		    rindex("foobar", "foobar", 8));
		printf("   D => rindex(\"foobar\", \"foobar\", 8) = 0\n");
		$failed++;
	}

	if (index("foobar", "foobar", 400) != -1) {
		printf("perl => index(\"foobar\", \"foobar\", 400) = %d\n",
		    index("foobar", "foobar", 400));
		printf("   D => index(\"foobar\", \"foobar\", 400) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobar", 400) != 0) {
		printf("perl => rindex(\"foobar\", \"foobar\", 400) = %d\n",
		    rindex("foobar", "foobar", 400));
		printf("   D => rindex(\"foobar\", \"foobar\", 400) = 0\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz", -400) != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\", -400) = %d\n",
		    index("foobar", "foobarbaz", -400));
		printf("   D => index(\"foobar\", \"foobarbaz\", -400) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz", -400) != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\", -400) = %d\n",
		    rindex("foobar", "foobarbaz", -400));
		printf("   D => rindex(\"foobar\", \"foobarbaz\", -400) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz", -1) != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\", -1) = %d\n",
		    index("foobar", "foobarbaz", -1));
		printf("   D => index(\"foobar\", \"foobarbaz\", -1) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz", -1) != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\", -1) = %d\n",
		    rindex("foobar", "foobarbaz", -1));
		printf("   D => rindex(\"foobar\", \"foobarbaz\", -1) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz", 0) != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\", 0) = %d\n",
		    index("foobar", "foobarbaz", 0));
		printf("   D => index(\"foobar\", \"foobarbaz\", 0) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz", 0) != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\", 0) = %d\n",
		    rindex("foobar", "foobarbaz", 0));
		printf("   D => rindex(\"foobar\", \"foobarbaz\", 0) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz", 3) != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\", 3) = %d\n",
		    index("foobar", "foobarbaz", 3));
		printf("   D => index(\"foobar\", \"foobarbaz\", 3) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz", 3) != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\", 3) = %d\n",
		    rindex("foobar", "foobarbaz", 3));
		printf("   D => rindex(\"foobar\", \"foobarbaz\", 3) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz", 6) != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\", 6) = %d\n",
		    index("foobar", "foobarbaz", 6));
		printf("   D => index(\"foobar\", \"foobarbaz\", 6) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz", 6) != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\", 6) = %d\n",
		    rindex("foobar", "foobarbaz", 6));
		printf("   D => rindex(\"foobar\", \"foobarbaz\", 6) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz", 7) != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\", 7) = %d\n",
		    index("foobar", "foobarbaz", 7));
		printf("   D => index(\"foobar\", \"foobarbaz\", 7) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz", 7) != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\", 7) = %d\n",
		    rindex("foobar", "foobarbaz", 7));
		printf("   D => rindex(\"foobar\", \"foobarbaz\", 7) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz", 8) != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\", 8) = %d\n",
		    index("foobar", "foobarbaz", 8));
		printf("   D => index(\"foobar\", \"foobarbaz\", 8) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz", 8) != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\", 8) = %d\n",
		    rindex("foobar", "foobarbaz", 8));
		printf("   D => rindex(\"foobar\", \"foobarbaz\", 8) = -1\n");
		$failed++;
	}

	if (index("foobar", "foobarbaz", 400) != -1) {
		printf("perl => index(\"foobar\", \"foobarbaz\", 400) = %d\n",
		    index("foobar", "foobarbaz", 400));
		printf("   D => index(\"foobar\", \"foobarbaz\", 400) = -1\n");
		$failed++;
	}

	if (rindex("foobar", "foobarbaz", 400) != -1) {
		printf("perl => rindex(\"foobar\", \"foobarbaz\", 400) = %d\n",
		    rindex("foobar", "foobarbaz", 400));
		printf("   D => rindex(\"foobar\", \"foobarbaz\", 400) = -1\n");
		$failed++;
	}

	if (index("", "foobar", -400) != -1) {
		printf("perl => index(\"\", \"foobar\", -400) = %d\n",
		    index("", "foobar", -400));
		printf("   D => index(\"\", \"foobar\", -400) = -1\n");
		$failed++;
	}

	if (rindex("", "foobar", -400) != -1) {
		printf("perl => rindex(\"\", \"foobar\", -400) = %d\n",
		    rindex("", "foobar", -400));
		printf("   D => rindex(\"\", \"foobar\", -400) = -1\n");
		$failed++;
	}

	if (index("", "foobar", -1) != -1) {
		printf("perl => index(\"\", \"foobar\", -1) = %d\n",
		    index("", "foobar", -1));
		printf("   D => index(\"\", \"foobar\", -1) = -1\n");
		$failed++;
	}

	if (rindex("", "foobar", -1) != -1) {
		printf("perl => rindex(\"\", \"foobar\", -1) = %d\n",
		    rindex("", "foobar", -1));
		printf("   D => rindex(\"\", \"foobar\", -1) = -1\n");
		$failed++;
	}

	if (index("", "foobar", 0) != -1) {
		printf("perl => index(\"\", \"foobar\", 0) = %d\n",
		    index("", "foobar", 0));
		printf("   D => index(\"\", \"foobar\", 0) = -1\n");
		$failed++;
	}

	if (rindex("", "foobar", 0) != -1) {
		printf("perl => rindex(\"\", \"foobar\", 0) = %d\n",
		    rindex("", "foobar", 0));
		printf("   D => rindex(\"\", \"foobar\", 0) = -1\n");
		$failed++;
	}

	if (index("", "foobar", 0) != -1) {
		printf("perl => index(\"\", \"foobar\", 0) = %d\n",
		    index("", "foobar", 0));
		printf("   D => index(\"\", \"foobar\", 0) = -1\n");
		$failed++;
	}

	if (rindex("", "foobar", 0) != -1) {
		printf("perl => rindex(\"\", \"foobar\", 0) = %d\n",
		    rindex("", "foobar", 0));
		printf("   D => rindex(\"\", \"foobar\", 0) = -1\n");
		$failed++;
	}

	if (index("", "foobar", 0) != -1) {
		printf("perl => index(\"\", \"foobar\", 0) = %d\n",
		    index("", "foobar", 0));
		printf("   D => index(\"\", \"foobar\", 0) = -1\n");
		$failed++;
	}

	if (rindex("", "foobar", 0) != -1) {
		printf("perl => rindex(\"\", \"foobar\", 0) = %d\n",
		    rindex("", "foobar", 0));
		printf("   D => rindex(\"\", \"foobar\", 0) = -1\n");
		$failed++;
	}

	if (index("", "foobar", 1) != -1) {
		printf("perl => index(\"\", \"foobar\", 1) = %d\n",
		    index("", "foobar", 1));
		printf("   D => index(\"\", \"foobar\", 1) = -1\n");
		$failed++;
	}

	if (rindex("", "foobar", 1) != -1) {
		printf("perl => rindex(\"\", \"foobar\", 1) = %d\n",
		    rindex("", "foobar", 1));
		printf("   D => rindex(\"\", \"foobar\", 1) = -1\n");
		$failed++;
	}

	if (index("", "foobar", 2) != -1) {
		printf("perl => index(\"\", \"foobar\", 2) = %d\n",
		    index("", "foobar", 2));
		printf("   D => index(\"\", \"foobar\", 2) = -1\n");
		$failed++;
	}

	if (rindex("", "foobar", 2) != -1) {
		printf("perl => rindex(\"\", \"foobar\", 2) = %d\n",
		    rindex("", "foobar", 2));
		printf("   D => rindex(\"\", \"foobar\", 2) = -1\n");
		$failed++;
	}

	if (index("", "foobar", 400) != -1) {
		printf("perl => index(\"\", \"foobar\", 400) = %d\n",
		    index("", "foobar", 400));
		printf("   D => index(\"\", \"foobar\", 400) = -1\n");
		$failed++;
	}

	if (rindex("", "foobar", 400) != -1) {
		printf("perl => rindex(\"\", \"foobar\", 400) = %d\n",
		    rindex("", "foobar", 400));
		printf("   D => rindex(\"\", \"foobar\", 400) = -1\n");
		$failed++;
	}

	if (index("foobar", "", -400) != 0) {
		printf("perl => index(\"foobar\", \"\", -400) = %d\n",
		    index("foobar", "", -400));
		printf("   D => index(\"foobar\", \"\", -400) = 0\n");
		$failed++;
	}

	if (rindex("foobar", "", -400) != 0) {
		printf("perl => rindex(\"foobar\", \"\", -400) = %d\n",
		    rindex("foobar", "", -400));
		printf("   D => rindex(\"foobar\", \"\", -400) = 0\n");
		$failed++;
	}

	if (index("foobar", "", -1) != 0) {
		printf("perl => index(\"foobar\", \"\", -1) = %d\n",
		    index("foobar", "", -1));
		printf("   D => index(\"foobar\", \"\", -1) = 0\n");
		$failed++;
	}

	if (rindex("foobar", "", -1) != 0) {
		printf("perl => rindex(\"foobar\", \"\", -1) = %d\n",
		    rindex("foobar", "", -1));
		printf("   D => rindex(\"foobar\", \"\", -1) = 0\n");
		$failed++;
	}

	if (index("foobar", "", 0) != 0) {
		printf("perl => index(\"foobar\", \"\", 0) = %d\n",
		    index("foobar", "", 0));
		printf("   D => index(\"foobar\", \"\", 0) = 0\n");
		$failed++;
	}

	if (rindex("foobar", "", 0) != 0) {
		printf("perl => rindex(\"foobar\", \"\", 0) = %d\n",
		    rindex("foobar", "", 0));
		printf("   D => rindex(\"foobar\", \"\", 0) = 0\n");
		$failed++;
	}

	if (index("foobar", "", 3) != 3) {
		printf("perl => index(\"foobar\", \"\", 3) = %d\n",
		    index("foobar", "", 3));
		printf("   D => index(\"foobar\", \"\", 3) = 3\n");
		$failed++;
	}

	if (rindex("foobar", "", 3) != 3) {
		printf("perl => rindex(\"foobar\", \"\", 3) = %d\n",
		    rindex("foobar", "", 3));
		printf("   D => rindex(\"foobar\", \"\", 3) = 3\n");
		$failed++;
	}

	if (index("foobar", "", 6) != 6) {
		printf("perl => index(\"foobar\", \"\", 6) = %d\n",
		    index("foobar", "", 6));
		printf("   D => index(\"foobar\", \"\", 6) = 6\n");
		$failed++;
	}

	if (rindex("foobar", "", 6) != 6) {
		printf("perl => rindex(\"foobar\", \"\", 6) = %d\n",
		    rindex("foobar", "", 6));
		printf("   D => rindex(\"foobar\", \"\", 6) = 6\n");
		$failed++;
	}

	if (index("foobar", "", 7) != 6) {
		printf("perl => index(\"foobar\", \"\", 7) = %d\n",
		    index("foobar", "", 7));
		printf("   D => index(\"foobar\", \"\", 7) = 6\n");
		$failed++;
	}

	if (rindex("foobar", "", 7) != 6) {
		printf("perl => rindex(\"foobar\", \"\", 7) = %d\n",
		    rindex("foobar", "", 7));
		printf("   D => rindex(\"foobar\", \"\", 7) = 6\n");
		$failed++;
	}

	if (index("foobar", "", 8) != 6) {
		printf("perl => index(\"foobar\", \"\", 8) = %d\n",
		    index("foobar", "", 8));
		printf("   D => index(\"foobar\", \"\", 8) = 6\n");
		$failed++;
	}

	if (rindex("foobar", "", 8) != 6) {
		printf("perl => rindex(\"foobar\", \"\", 8) = %d\n",
		    rindex("foobar", "", 8));
		printf("   D => rindex(\"foobar\", \"\", 8) = 6\n");
		$failed++;
	}

	if (index("foobar", "", 400) != 6) {
		printf("perl => index(\"foobar\", \"\", 400) = %d\n",
		    index("foobar", "", 400));
		printf("   D => index(\"foobar\", \"\", 400) = 6\n");
		$failed++;
	}

	if (rindex("foobar", "", 400) != 6) {
		printf("perl => rindex(\"foobar\", \"\", 400) = %d\n",
		    rindex("foobar", "", 400));
		printf("   D => rindex(\"foobar\", \"\", 400) = 6\n");
		$failed++;
	}

	if (index("", "", -400) != 0) {
		printf("perl => index(\"\", \"\", -400) = %d\n",
		    index("", "", -400));
		printf("   D => index(\"\", \"\", -400) = 0\n");
		$failed++;
	}

	if (rindex("", "", -400) != 0) {
		printf("perl => rindex(\"\", \"\", -400) = %d\n",
		    rindex("", "", -400));
		printf("   D => rindex(\"\", \"\", -400) = 0\n");
		$failed++;
	}

	if (index("", "", -1) != 0) {
		printf("perl => index(\"\", \"\", -1) = %d\n",
		    index("", "", -1));
		printf("   D => index(\"\", \"\", -1) = 0\n");
		$failed++;
	}

	if (rindex("", "", -1) != 0) {
		printf("perl => rindex(\"\", \"\", -1) = %d\n",
		    rindex("", "", -1));
		printf("   D => rindex(\"\", \"\", -1) = 0\n");
		$failed++;
	}

	if (index("", "", 0) != 0) {
		printf("perl => index(\"\", \"\", 0) = %d\n",
		    index("", "", 0));
		printf("   D => index(\"\", \"\", 0) = 0\n");
		$failed++;
	}

	if (rindex("", "", 0) != 0) {
		printf("perl => rindex(\"\", \"\", 0) = %d\n",
		    rindex("", "", 0));
		printf("   D => rindex(\"\", \"\", 0) = 0\n");
		$failed++;
	}

	if (index("", "", 0) != 0) {
		printf("perl => index(\"\", \"\", 0) = %d\n",
		    index("", "", 0));
		printf("   D => index(\"\", \"\", 0) = 0\n");
		$failed++;
	}

	if (rindex("", "", 0) != 0) {
		printf("perl => rindex(\"\", \"\", 0) = %d\n",
		    rindex("", "", 0));
		printf("   D => rindex(\"\", \"\", 0) = 0\n");
		$failed++;
	}

	if (index("", "", 0) != 0) {
		printf("perl => index(\"\", \"\", 0) = %d\n",
		    index("", "", 0));
		printf("   D => index(\"\", \"\", 0) = 0\n");
		$failed++;
	}

	if (rindex("", "", 0) != 0) {
		printf("perl => rindex(\"\", \"\", 0) = %d\n",
		    rindex("", "", 0));
		printf("   D => rindex(\"\", \"\", 0) = 0\n");
		$failed++;
	}

	if (index("", "", 1) != 0) {
		printf("perl => index(\"\", \"\", 1) = %d\n",
		    index("", "", 1));
		printf("   D => index(\"\", \"\", 1) = 0\n");
		$failed++;
	}

	if (rindex("", "", 1) != 0) {
		printf("perl => rindex(\"\", \"\", 1) = %d\n",
		    rindex("", "", 1));
		printf("   D => rindex(\"\", \"\", 1) = 0\n");
		$failed++;
	}

	exit($failed);
}

