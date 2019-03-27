// HTML Named Character Reference
class NCR<string spelling, int codePoint> {
  string Spelling = spelling;
  int CodePoint = codePoint;
}

// The list below includes named character references supported by Doxygen:
// http://www.stack.nl/~dimitri/doxygen/manual/htmlcmds.html
//
// It does not include all HTML 5 named character references.
//
// Corresponding code point values can be found here:
// http://www.w3.org/TR/2011/WD-html5-20110113/named-character-references.html

def : NCR<"copy",  0x000A9>;
def : NCR<"COPY",  0x000A9>;
def : NCR<"trade", 0x02122>;
def : NCR<"TRADE", 0x02122>;
def : NCR<"reg",   0x000AE>;
def : NCR<"REG",   0x000AE>;
def : NCR<"lt",    0x0003C>;
def : NCR<"Lt",    0x0003C>;
def : NCR<"LT",    0x0003C>;
def : NCR<"gt",    0x0003E>;
def : NCR<"Gt",    0x0003E>;
def : NCR<"GT",    0x0003E>;
def : NCR<"amp",   0x00026>;
def : NCR<"AMP",   0x00026>;
def : NCR<"apos",  0x00027>;
def : NCR<"quot",  0x00022>;
def : NCR<"QUOT",  0x00022>;
def : NCR<"lsquo", 0x02018>;
def : NCR<"rsquo", 0x02019>;
def : NCR<"ldquo", 0x0201C>;
def : NCR<"rdquo", 0x0201D>;
def : NCR<"ndash", 0x02013>;
def : NCR<"mdash", 0x02014>;

def : NCR<"Auml", 0x000C4>;
def : NCR<"Euml", 0x000CB>;
def : NCR<"Iuml", 0x000CF>;
def : NCR<"Ouml", 0x000D6>;
def : NCR<"Uuml", 0x000DC>;
def : NCR<"Yuml", 0x00178>;
def : NCR<"auml", 0x000E4>;
def : NCR<"euml", 0x000EB>;
def : NCR<"iuml", 0x000EF>;
def : NCR<"ouml", 0x000F6>;
def : NCR<"uuml", 0x000FC>;
def : NCR<"yuml", 0x000FF>;

def : NCR<"Aacute", 0x000C1>;
def : NCR<"Eacute", 0x000C9>;
def : NCR<"Iacute", 0x000CD>;
def : NCR<"Oacute", 0x000D3>;
def : NCR<"Uacute", 0x000DA>;
def : NCR<"Yacute", 0x000DD>;
def : NCR<"aacute", 0x000E1>;
def : NCR<"eacute", 0x000E9>;
def : NCR<"iacute", 0x000ED>;
def : NCR<"oacute", 0x000F3>;
def : NCR<"uacute", 0x000FA>;
def : NCR<"yacute", 0x000FD>;

def : NCR<"Agrave", 0x000C0>;
def : NCR<"Egrave", 0x000C8>;
def : NCR<"Igrave", 0x000CC>;
def : NCR<"Ograve", 0x000D2>;
def : NCR<"Ugrave", 0x000D9>;
// def : NCR<"Ygrave", 0x01EF2>; // Defined neither in Doxygen, nor in HTML5.
def : NCR<"agrave", 0x000E0>;
def : NCR<"egrave", 0x000E8>;
def : NCR<"igrave", 0x000EC>;
def : NCR<"ograve", 0x000F2>;
def : NCR<"ugrave", 0x000F9>;
def : NCR<"ygrave", 0x01EF3>; // Defined in Doxygen, not defined in HTML5.

def : NCR<"Acirc", 0x000C2>;
def : NCR<"Ecirc", 0x000CA>;
def : NCR<"Icirc", 0x000CE>;
def : NCR<"Ocirc", 0x000D4>;
def : NCR<"Ucirc", 0x000DB>;
def : NCR<"Ycirc", 0x00176>; // Not defined in Doxygen, defined in HTML5.
def : NCR<"acirc", 0x000E2>;
def : NCR<"ecirc", 0x000EA>;
def : NCR<"icirc", 0x000EE>;
def : NCR<"ocirc", 0x000F4>;
def : NCR<"ucirc", 0x000FB>;
def : NCR<"ycirc", 0x00177>;

def : NCR<"Atilde", 0x000C3>;
def : NCR<"Ntilde", 0x000D1>;
def : NCR<"Otilde", 0x000D5>;
def : NCR<"atilde", 0x000E3>;
def : NCR<"ntilde", 0x000F1>;
def : NCR<"otilde", 0x000F5>;

def : NCR<"szlig", 0x000DF>;

def : NCR<"ccedil", 0x000E7>;
def : NCR<"Ccedil", 0x000C7>;

def : NCR<"aring", 0x000E5>;
def : NCR<"Aring", 0x000C5>;

def : NCR<"nbsp", 0x000A0>;

def : NCR<"Gamma",   0x00393>;
def : NCR<"Delta",   0x00394>;
def : NCR<"Theta",   0x00398>;
def : NCR<"Lambda",  0x0039B>;
def : NCR<"Xi",      0x0039E>;
def : NCR<"Pi",      0x003A0>;
def : NCR<"Sigma",   0x003A3>;
def : NCR<"Upsilon", 0x003A5>;
def : NCR<"Phi",     0x003A6>;
def : NCR<"Psi",     0x003A8>;
def : NCR<"Omega",   0x003A9>;

def : NCR<"alpha",   0x003B1>;
def : NCR<"beta",    0x003B2>;
def : NCR<"gamma",   0x003B3>;
def : NCR<"delta",   0x003B4>;
def : NCR<"epsilon", 0x003B5>;
def : NCR<"zeta",    0x003B6>;
def : NCR<"eta",     0x003B7>;
def : NCR<"theta",   0x003B8>;
def : NCR<"iota",    0x003B9>;
def : NCR<"kappa",   0x003BA>;
def : NCR<"lambda",  0x003BB>;
def : NCR<"mu",      0x003BC>;
def : NCR<"nu",      0x003BD>;
def : NCR<"xi",      0x003BE>;
def : NCR<"pi",      0x003C0>;
def : NCR<"rho",     0x003C1>;
def : NCR<"sigma",   0x003C3>;
def : NCR<"tau",     0x003C4>;
def : NCR<"upsilon", 0x003C5>;
def : NCR<"phi",     0x003C6>;
def : NCR<"chi",     0x003C7>;
def : NCR<"psi",     0x003C8>;
def : NCR<"omega",   0x003C9>;
def : NCR<"sigmaf",  0x003C2>;

def : NCR<"sect",   0x000A7>;
def : NCR<"deg",    0x000B0>;
def : NCR<"prime",  0x02032>;
def : NCR<"Prime",  0x02033>;
def : NCR<"infin",  0x0221E>;
def : NCR<"empty",  0x02205>;
def : NCR<"plusmn", 0x000B1>;
def : NCR<"times",  0x000D7>;
def : NCR<"minus",  0x02212>;
def : NCR<"sdot",   0x022C5>;
def : NCR<"part",   0x02202>;
def : NCR<"nabla",  0x02207>;
def : NCR<"radic",  0x0221A>;
def : NCR<"perp",   0x022A5>;
def : NCR<"sum",    0x02211>;
def : NCR<"int",    0x0222B>;
def : NCR<"prod",   0x0220F>;
def : NCR<"sim",    0x0223C>;
def : NCR<"asymp",  0x02248>;
def : NCR<"ne",     0x02260>;
def : NCR<"equiv",  0x02261>;
def : NCR<"prop",   0x0221D>;
def : NCR<"le",     0x02264>;
def : NCR<"ge",     0x02265>;
def : NCR<"larr",   0x02190>;
def : NCR<"rarr",   0x02192>;
def : NCR<"isin",   0x02208>;
def : NCR<"notin",  0x02209>;
def : NCR<"lceil",  0x02308>;
def : NCR<"rceil",  0x02309>;
def : NCR<"lfloor", 0x0230A>;
def : NCR<"rfloor", 0x0230B>;

