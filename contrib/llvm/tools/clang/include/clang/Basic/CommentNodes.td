class Comment<bit abstract = 0> {
  bit Abstract = abstract;
}

class DComment<Comment base, bit abstract = 0> : Comment<abstract> {
  Comment Base = base;
}

def InlineContentComment : Comment<1>;
  def TextComment : DComment<InlineContentComment>;
  def InlineCommandComment : DComment<InlineContentComment>;
  def HTMLTagComment : DComment<InlineContentComment, 1>;
    def HTMLStartTagComment : DComment<HTMLTagComment>;
    def HTMLEndTagComment : DComment<HTMLTagComment>;

def BlockContentComment : Comment<1>;
  def ParagraphComment : DComment<BlockContentComment>;
  def BlockCommandComment : DComment<BlockContentComment>;
    def ParamCommandComment : DComment<BlockCommandComment>;
    def TParamCommandComment : DComment<BlockCommandComment>;
    def VerbatimBlockComment : DComment<BlockCommandComment>;
    def VerbatimLineComment : DComment<BlockCommandComment>;

def VerbatimBlockLineComment : Comment;

def FullComment : Comment;

