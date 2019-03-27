class Tag<string spelling> {
  string Spelling = spelling;
  bit EndTagOptional = 0;
  bit EndTagForbidden = 0;
}

def Em      : Tag<"em">;
def Strong  : Tag<"strong">;
def Tt      : Tag<"tt">;
def I       : Tag<"i">;
def B       : Tag<"b">;
def Big     : Tag<"big">;
def Small   : Tag<"small">;
def Strike  : Tag<"strike">;
def S       : Tag<"s">;
def U       : Tag<"u">;
def Font    : Tag<"font">;
def A       : Tag<"a">;
def Hr      : Tag<"hr"> { let EndTagForbidden = 1; }
def Div     : Tag<"div">;
def Span    : Tag<"span">;
def H1      : Tag<"h1">;
def H2      : Tag<"h2">;
def H3      : Tag<"h3">;
def H4      : Tag<"h4">;
def H5      : Tag<"h5">;
def H6      : Tag<"h6">;
def Code    : Tag<"code">;
def Blockquote : Tag<"blockquote">;
def Sub     : Tag<"sub">;
def Sup     : Tag<"sup">;
def Img     : Tag<"img"> { let EndTagForbidden = 1; }
def P       : Tag<"p"> { let EndTagOptional = 1; }
def Br      : Tag<"br"> { let EndTagForbidden = 1; }
def Pre     : Tag<"pre">;
def Ins     : Tag<"ins">;
def Del     : Tag<"del">;
def Ul      : Tag<"ul">;
def Ol      : Tag<"ol">;
def Li      : Tag<"li"> { let EndTagOptional = 1; }
def Dl      : Tag<"dl">;
def Dt      : Tag<"dt"> { let EndTagOptional = 1; }
def Dd      : Tag<"dd"> { let EndTagOptional = 1; }
def Table   : Tag<"table">;
def Caption : Tag<"caption">;
def Thead   : Tag<"thead"> { let EndTagOptional = 1; }
def Tfoot   : Tag<"tfoot"> { let EndTagOptional = 1; }
def Tbody   : Tag<"tbody"> { let EndTagOptional = 1; }
def Colgroup : Tag<"colgroup"> { let EndTagOptional = 1; }
def Col     : Tag<"col"> { let EndTagForbidden = 1; }
def Tr      : Tag<"tr"> { let EndTagOptional = 1; }
def Th      : Tag<"th"> { let EndTagOptional = 1; }
def Td      : Tag<"td"> { let EndTagOptional = 1; }

// Define a blacklist of attributes that are not safe to pass through to HTML
// output if the input is untrusted.
//
// FIXME: this should be a whitelist.  When changing this to a whitelist, don't
// forget to change the default in the TableGen backend.
class Attribute<string spelling> {
  string Spelling = spelling;
  bit IsSafeToPassThrough = 1;
}
class EventHandlerContentAttribute<string spelling> : Attribute<spelling> {
  let IsSafeToPassThrough = 0;
}

