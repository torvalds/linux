/* output from Ox version G1.04 */
#line 1 "expr.Y"
#line 4 "expr.oxout.y"
%{
#include <stdlib.h>
#include <string.h>
%}
#line 1 "expr.Y"
/* Y-file for translation of infix expressions to prefix and postfix */
%token ID CONST  
%start yyyAugNonterm    
%left '+' '-'
%left '*' '/'
%nonassoc '*'

%{ 
#include "expr.oxout.h" 
#include <stdio.h>

extern int yylex(void);
extern void yyerror(const char *);
%} 

#line 25 "expr.oxout.y"

%{
#include <limits.h>
#define yyyR USHRT_MAX  
%}
%type <yyyOxAttrbs> yyyAugNonterm
%union {
struct yyyOxAttrbs {
struct yyyStackItem *yyyOxStackItem;
} yyyOxAttrbs;
}

%{
#include <stdio.h>
#include <stdarg.h>

static int yyyYok = 1;

extern yyyFT yyyRCIL[];

void yyyExecuteRRsection(yyyGNT *rootNode);
void yyyYoxInit(void);
void yyyDecorate(void);
struct yyyOxAttrbs; /* hack required to compensate for 'msta' behavior */
void yyyGenIntNode(long yyyProdNum, int yyyRHSlength, int yyyNattrbs, struct yyyOxAttrbs *yyval_OxAttrbs, ...);
void yyyAdjustINRC(long yyyProdNum, int yyyRHSlength, long startP, long stopP, struct yyyOxAttrbs *yyval_OxAttrbs, ...);
void yyyCheckUnsolvedInstTrav(yyyGNT *rootNode,long *nNZrc,long *cycleSum);
void yyyUnsolvedInstSearchTrav(yyyGNT *pNode);
void yyyUnsolvedInstSearchTravAux(yyyGNT *pNode);
void yyyabort(void);

%}


#line 20 "expr.Y"
%%

#line 63 "expr.oxout.y"
yyyAugNonterm 
	:	{yyyYoxInit();}
		s
		{
		 yyyDecorate(); yyyExecuteRRsection($<yyyOxAttrbs>2.yyyOxStackItem->node);
		}
	;
#line 21 "expr.Y"
s       :       expr
#line 73 "expr.oxout.y"
{if(yyyYok){
yyyGenIntNode(1,1,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1);
yyyAdjustINRC(1,1,0,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1);}}

#line 27 "expr.Y"
expr    :       expr    '*'     expr
#line 80 "expr.oxout.y"
{if(yyyYok){
yyyGenIntNode(2,3,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);
yyyAdjustINRC(2,3,0,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);}}

#line 31 "expr.Y"
        |       expr    '+'     expr
#line 87 "expr.oxout.y"
{if(yyyYok){
yyyGenIntNode(3,3,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);
yyyAdjustINRC(3,3,0,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);}}

#line 35 "expr.Y"
        |       expr    '/'     expr
#line 94 "expr.oxout.y"
{if(yyyYok){
yyyGenIntNode(4,3,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);
yyyAdjustINRC(4,3,0,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);}}

#line 39 "expr.Y"
        |       expr    '-'     expr
#line 101 "expr.oxout.y"
{if(yyyYok){
yyyGenIntNode(5,3,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);
yyyAdjustINRC(5,3,0,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);}}

#line 43 "expr.Y"
        |       '('     expr    ')'
#line 108 "expr.oxout.y"
{if(yyyYok){
yyyGenIntNode(6,3,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);
yyyAdjustINRC(6,3,0,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1,&$<yyyOxAttrbs>2,&$<yyyOxAttrbs>3);}}
#line 44 "expr.Y"
        |       ID
#line 114 "expr.oxout.y"
{if(yyyYok){
yyyGenIntNode(7,1,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1);
yyyAdjustINRC(7,1,0,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1);}}

#line 48 "expr.Y"
        |       CONST
#line 121 "expr.oxout.y"
{if(yyyYok){
yyyGenIntNode(8,1,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1);
yyyAdjustINRC(8,1,0,0,&$<yyyOxAttrbs>$,&$<yyyOxAttrbs>1);}}

#line 52 "expr.Y"
        ;
%% 

int yyparse(void);

int main() 
  {yyparse(); 
  } 



#line 138 "expr.oxout.y"
long yyySSALspaceSize =    20000; 
long yyyRSmaxSize =        1000; 
long yyyTravStackMaxSize = 2000; 

struct yyySolvedSAlistCell {yyyWAT attrbNum; 
                            long next; 
                           }; 
 
#define yyyLambdaSSAL 0 
long yyySSALCfreeList = yyyLambdaSSAL; 
long yyyNewSSALC = 1; 
 
struct yyySolvedSAlistCell *yyySSALspace; 

long yyyNbytesStackStg; 



yyyFT yyyRCIL[1];

short yyyIIIEL[] = {0,
0,2,6,10,14,18,22,24,
};

long yyyIIEL[] = {
0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,
1,1,
};

long yyyIEL[] = {
0,0,0,
};

yyyFT yyyEntL[1];

void yyyfatal(char *msg)
{fputs(msg,stderr);exit(-1);} 



#define yyySSALof 'S' 
#define yyyRSof   'q' 
#define yyyTSof   't' 



void yyyHandleOverflow(char which) 
  {char *msg1,*msg2; 
   long  oldSize,newSize; 
   switch(which) 
     {
      case yyySSALof : 
           msg1 = "SSAL overflow: ";
           oldSize = yyySSALspaceSize; 
           break; 
      case yyyRSof   : 
           msg1 = "ready set overflow: ";
           oldSize = yyyRSmaxSize; 
           break; 
      case yyyTSof   : 
           msg1 = "traversal stack overflow: ";
           oldSize = yyyTravStackMaxSize; 
           break; 
      default        :;  
     }
   newSize = (3*oldSize)/2; 
   if (newSize < 100) newSize = 100; 
   fputs(msg1,stderr); 
   fprintf(stderr,"size was %ld.\n",oldSize); 
   msg2 = "     Have to modify evaluator:  -Y%c%ld.\n"; 
   fprintf(stderr,msg2,which,newSize); 
   exit(-1); 
  }



void yyySignalEnts(yyyGNT *node,long startP,long stopP) 
  {yyyGNT *dumNode; 

   while (startP < stopP)  
     {
      if (!yyyEntL[startP]) dumNode = node;  
         else dumNode = (node->cL)[yyyEntL[startP]-1];   
      if (!(--((dumNode->refCountList)[yyyEntL[startP+1]]
              ) 
           )
         ) 
         { 
          if (++yyyRSTop == yyyAfterRS) 
             {yyyHandleOverflow(yyyRSof); 
              break; 
             }
          yyyRSTop->node = dumNode; 
          yyyRSTop->whichSym = yyyEntL[startP];  
          yyyRSTop->wa = yyyEntL[startP+1];  
         }  
      startP += 2;  
     }  
  } 






void yyySolveAndSignal() {
long yyyiDum,*yyypL;
int yyyws,yyywa;
yyyGNT *yyyRSTopN,*yyyRefN; 
yyyParent yyyRSTopNp; 


yyyRSTopNp = (yyyRSTopN = yyyRSTop->node)->parent;
yyyRefN= (yyyws = (yyyRSTop->whichSym))?yyyRSTopNp.noderef:yyyRSTopN;
yyywa = yyyRSTop->wa; 
yyyRSTop--;
switch(yyyRefN->prodNum) {
case 1:  /***yacc rule 1***/
  switch (yyyws) {
  }
break;
case 2:  /***yacc rule 2***/
  switch (yyyws) {
  }
break;
case 3:  /***yacc rule 3***/
  switch (yyyws) {
  }
break;
case 4:  /***yacc rule 4***/
  switch (yyyws) {
  }
break;
case 5:  /***yacc rule 5***/
  switch (yyyws) {
  }
break;
case 6:  /***yacc rule 6***/
  switch (yyyws) {
  }
break;
case 7:  /***yacc rule 7***/
  switch (yyyws) {
  case 1:  /**/
    switch (yyywa) {
    }
  break;
  }
break;
case 8:  /***yacc rule 8***/
  switch (yyyws) {
  case 1:  /**/
    switch (yyywa) {
    }
  break;
  }
break;
} /* switch */ 

if (yyyws)  /* the just-solved instance was inherited. */ 
   {if (yyyRSTopN->prodNum) 
       {yyyiDum = yyyIIEL[yyyIIIEL[yyyRSTopN->prodNum]] + yyywa;
        yyySignalEnts(yyyRSTopN,yyyIEL[yyyiDum],
                                yyyIEL[yyyiDum+1]
                     );
       }
   } 
   else     /* the just-solved instance was synthesized. */ 
   {if (!(yyyRSTopN->parentIsStack)) /* node has a parent. */ 
       {yyyiDum = yyyIIEL[yyyIIIEL[yyyRSTopNp.noderef->prodNum] + 
                          yyyRSTopN->whichSym 
                         ] + 
                  yyywa;
        yyySignalEnts(yyyRSTopNp.noderef,
                      yyyIEL[yyyiDum],
                      yyyIEL[yyyiDum+1] 
                     );
       } 
       else   /* node is still on the stack--it has no parent yet. */ 
       {yyypL = &(yyyRSTopNp.stackref->solvedSAlist); 
        if (yyySSALCfreeList == yyyLambdaSSAL) 
           {yyySSALspace[yyyNewSSALC].next = *yyypL; 
            if ((*yyypL = yyyNewSSALC++) == yyySSALspaceSize) 
               yyyHandleOverflow(yyySSALof); 
           }  
           else
           {yyyiDum = yyySSALCfreeList; 
            yyySSALCfreeList = yyySSALspace[yyySSALCfreeList].next; 
            yyySSALspace[yyyiDum].next = *yyypL; 
            *yyypL = yyyiDum;  
           } 
        yyySSALspace[*yyypL].attrbNum = yyywa; 
       } 
   }

} /* yyySolveAndSignal */ 






#define condStg unsigned int conds;
#define yyyClearConds {yyyTST->conds = 0;}
#define yyySetCond(n) {yyyTST->conds += (1<<(n));}
#define yyyCond(n) ((yyyTST->conds & (1<<(n)))?1:0)



struct yyyTravStackItem {yyyGNT *node; 
                         char isReady;
                         condStg
                        };



void yyyDoTraversals(yyyGNT *rootNode)
{struct yyyTravStackItem *yyyTravStack,*yyyTST,*yyyAfterTravStack;
 yyyGNT *yyyTSTn,**yyyCLptr2; 
 int yyyi,yyyRL,yyyPass;
 int i;

 if (!yyyYok) return;
 if ((yyyTravStack = 
                 ((struct yyyTravStackItem *) 
                  calloc((size_t)yyyTravStackMaxSize, 
                         (size_t)sizeof(struct yyyTravStackItem)
                        )
                 )
     )
     == 
     (struct yyyTravStackItem *)NULL
    ) 
    {fputs("malloc error in traversal stack allocation\n",stderr); 
     exit(-1); 
    } 

yyyAfterTravStack = yyyTravStack + yyyTravStackMaxSize; 
yyyTravStack++; 


for (yyyi=0; yyyi<2; yyyi++) {
yyyTST = yyyTravStack; 
yyyTST->node = rootNode;
yyyTST->isReady = 0;
yyyClearConds

while(yyyTST >= yyyTravStack)
  {yyyTSTn = yyyTST->node;
   if (yyyTST->isReady)  
      {yyyPass = 1;
       goto yyyTravSwitch;
yyyTpop:
       yyyTST--;
      } 
      else 
      {yyyPass = 0;
       goto yyyTravSwitch;
yyyTpush:
       yyyTST->isReady = 1;  
       if (yyyTSTn->prodNum)
          {if (yyyRL)
             {yyyCLptr2 = yyyTSTn->cL; 
              i = yyyTSTn->cLlen; 
              while (i--) 
                {if (++yyyTST == yyyAfterTravStack)
                    yyyHandleOverflow(yyyTSof);
                    else
                    {yyyTST->node = *yyyCLptr2; 
                     yyyTST->isReady = 0; 
                     yyyClearConds
                    }
                 yyyCLptr2++; 
                } 
             } /* right to left */
             else  /* left to right */
             {i = yyyTSTn->cLlen; 
              yyyCLptr2 = yyyTSTn->cL + i; 
              while (i--) 
                {yyyCLptr2--; 
                 if (++yyyTST == yyyAfterTravStack)
                    yyyHandleOverflow(yyyTSof);
                    else
                    {yyyTST->node = *yyyCLptr2; 
                     yyyTST->isReady = 0; 
                     yyyClearConds
                    }
                } 
             } /* left to right */
          }
      } /* else */
   continue;
yyyTravSwitch:
				switch(yyyTSTn->prodNum)	{
case 1:
	switch(yyyi)	{ 
		case 0:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;yyySetCond(0)

if (!
#line 24 "expr.Y"
  (1)
#line 444 "expr.oxout.y"
) yyySetCond(1)
yyySetCond(2)

				case 1:

if (yyyCond(0) != yyyPass) {
#line 24 "expr.Y"
  
#line 453 "expr.oxout.y"
}
if (yyyCond(1) != yyyPass) {
#line 24 "expr.Y"
 printf("\n"); 
                   
#line 459 "expr.oxout.y"
}
if (yyyCond(2) != yyyPass) {
#line 25 "expr.Y"
  printf("prefix:   ");
                
#line 465 "expr.oxout.y"
}
				break;
					}
		break;
		case 1:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
if (
#line 23 "expr.Y"
  (1)
#line 477 "expr.oxout.y"
) yyySetCond(2)

				case 1:

if (yyyCond(0) != yyyPass) {
#line 22 "expr.Y"
 printf("\n"); 
                   
#line 486 "expr.oxout.y"
}
if (yyyCond(1) != yyyPass) {
#line 23 "expr.Y"
 
#line 491 "expr.oxout.y"
}
if (yyyCond(2) != yyyPass) {
#line 23 "expr.Y"
 printf("postfix:  ")/* missing ; */
                   
#line 497 "expr.oxout.y"
}
				break;
					}
		break;
			}

break;
case 2:
	switch(yyyi)	{ 
		case 0:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;yyySetCond(0)

				case 1:

if (yyyCond(0) != yyyPass) {
#line 29 "expr.Y"
  printf(" * "); 
                
#line 518 "expr.oxout.y"
}
				break;
					}
		break;
		case 1:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
				case 1:

if (yyyCond(0) != yyyPass) {
#line 28 "expr.Y"
 printf(" * "); 
                   
#line 533 "expr.oxout.y"
}
				break;
					}
		break;
			}

break;
case 3:
	switch(yyyi)	{ 
		case 0:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;yyySetCond(0)

				case 1:

if (yyyCond(0) != yyyPass) {
#line 32 "expr.Y"
  printf(" + "); 
                   
#line 554 "expr.oxout.y"
}
				break;
					}
		break;
		case 1:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
				case 1:

if (yyyCond(0) != yyyPass) {
#line 33 "expr.Y"
 printf(" + "); 
                
#line 569 "expr.oxout.y"
}
				break;
					}
		break;
			}

break;
case 4:
	switch(yyyi)	{ 
		case 0:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;yyySetCond(0)

				case 1:

if (yyyCond(0) != yyyPass) {
#line 37 "expr.Y"
  printf(" / "); 
                
#line 590 "expr.oxout.y"
}
				break;
					}
		break;
		case 1:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
				case 1:

if (yyyCond(0) != yyyPass) {
#line 36 "expr.Y"
 printf(" / "); 
                   
#line 605 "expr.oxout.y"
}
				break;
					}
		break;
			}

break;
case 5:
	switch(yyyi)	{ 
		case 0:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;yyySetCond(0)

				case 1:

if (yyyCond(0) != yyyPass) {
#line 41 "expr.Y"
  printf(" - "); 
                
#line 626 "expr.oxout.y"
}
				break;
					}
		break;
		case 1:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
				case 1:

if (yyyCond(0) != yyyPass) {
#line 40 "expr.Y"
 printf(" - "); 
                   
#line 641 "expr.oxout.y"
}
				break;
					}
		break;
			}

break;
case 6:
	switch(yyyi)	{ 
		case 0:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
				case 1:

				break;
					}
		break;
		case 1:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
				case 1:

				break;
					}
		break;
			}

break;
case 7:
	switch(yyyi)	{ 
		case 0:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;yyySetCond(0)

				case 1:

if (yyyCond(0) != yyyPass) {
#line 46 "expr.Y"
  printf(" %s ",yyyTSTn->cL[0]->yyyAttrbs.yyyAttrb1.lexeme); 
                
#line 685 "expr.oxout.y"
}
				break;
					}
		break;
		case 1:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
				case 1:

if (yyyCond(0) != yyyPass) {
#line 45 "expr.Y"
 printf(" %s ",yyyTSTn->cL[0]->yyyAttrbs.yyyAttrb1.lexeme); 
                   
#line 700 "expr.oxout.y"
}
				break;
					}
		break;
			}

break;
case 8:
	switch(yyyi)	{ 
		case 0:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;yyySetCond(0)

				case 1:

if (yyyCond(0) != yyyPass) {
#line 50 "expr.Y"
  printf(" %s ",yyyTSTn->cL[0]->yyyAttrbs.yyyAttrb1.lexeme); 
                
#line 721 "expr.oxout.y"
}
				break;
					}
		break;
		case 1:
			switch(yyyPass)	{
				case 0:
yyyRL = 0;
				case 1:

if (yyyCond(0) != yyyPass) {
#line 49 "expr.Y"
 printf(" %s ",yyyTSTn->cL[0]->yyyAttrbs.yyyAttrb1.lexeme); 
                   
#line 736 "expr.oxout.y"
}
				break;
					}
		break;
			}

break;
								} /* switch */ 
   if (yyyPass) goto yyyTpop; else goto yyyTpush; 
  } /* while */ 
 } /* for */ 
} /* yyyDoTraversals */ 

void yyyExecuteRRsection(yyyGNT *rootNode)  {
   int yyyi; 
   long cycleSum = 0; 
   long nNZrc = 0; 

   if (!yyyYok) return; 
   yyyCheckUnsolvedInstTrav(rootNode,&nNZrc,&cycleSum);
   if (nNZrc) 
      {
       fputs("\n\n\n**********\n",stderr);
       fputs("cycle detected in completed parse tree",stderr);
       fputs(" after decoration.\n",stderr);
#if CYCLE_VERBOSE
       fprintf(stderr,
               "number of unsolved attribute instances == %ld.\n", 
               nNZrc 
              ); 
       fprintf(stderr,
               "total number of remaining dependencies == %ld.\n", 
               cycleSum
              ); 
       fputs("average number of remaining dependencies\n",stderr);
       fprintf(stderr,"  per unsolved instance == %f.\n", 
               ((float)(cycleSum)/(float)(nNZrc)) 
              ); 
#endif
       fprintf(stderr,
         "searching parse tree for %ld unsolved instances:\n",
               nNZrc
              );
       yyyUnsolvedInstSearchTravAux(rootNode);
      }
   yyyDoTraversals(rootNode);
} /* yyyExecuteRRsection */ 



yyyWAT yyyLRCIL[2] = {0,0,
};



void yyyYoxInit(void) 
  { 
   static int yyyInitDone = 0;
   if (yyyInitDone) return;
 
   if ((yyyRS = (struct yyyRSitem *) 
         calloc((size_t)(yyyRSmaxSize+1), (size_t)sizeof(struct yyyRSitem))
       )  
       == 
       ((struct yyyRSitem *) NULL) 
      )   
      yyyfatal("malloc error in ox ready set space allocation\n");  
   yyyRS++; 
   yyyAfterRS = yyyRS + yyyRSmaxSize; 

 
   if ((yyySSALspace = (struct yyySolvedSAlistCell *) 
          calloc((size_t)(yyySSALspaceSize+1), (size_t)sizeof(struct yyySolvedSAlistCell)) 
       ) 
       == 
       ((struct yyySolvedSAlistCell *) NULL) 
      ) 
      yyyfatal("malloc error in stack solved list space allocation\n"); 
   yyyInitDone = 1;
 
   yyyRSTop = yyyRS - 1; 
  } /* yyyYoxInit */ 



void yyyDecorate(void) 
  { 
   while (yyyRSTop >= yyyRS) 
      yyySolveAndSignal();  
  } 



void yyyGenIntNode(long yyyProdNum, int yyyRHSlength, int yyyNattrbs, struct yyyOxAttrbs *yyval_OxAttrbs, ...) 
  {yyyWST i;
   yyySIT **yyyOxStackItem = &yyval_OxAttrbs->yyyOxStackItem; 
   yyyGNT *gnpDum; 
   va_list ap; 

   *yyyOxStackItem = (yyySIT *) malloc((size_t)sizeof(yyySIT)); 
   if (*yyyOxStackItem == (yyySIT *) NULL) 
      yyyfatal("malloc error in ox yacc semantic stack space allocation\n");
   (*yyyOxStackItem)->node = 
                                (yyyGNT *) malloc((size_t)sizeof(yyyGNT)); 
   if ((*yyyOxStackItem)->node == (yyyGNT *) NULL) 
      yyyfatal("malloc error in ox node space allocation\n");
   (*yyyOxStackItem)->solvedSAlist = yyyLambdaSSAL; 
   (*yyyOxStackItem)->node->parent.stackref = *yyyOxStackItem;  
   (*yyyOxStackItem)->node->parentIsStack = 1;  
   (*yyyOxStackItem)->node->cLlen  = yyyRHSlength; 
   (*yyyOxStackItem)->node->cL = 
            (yyyGNT **) calloc((size_t)yyyRHSlength, (size_t)sizeof(yyyGNT *)); 
   if ((*yyyOxStackItem)->node->cL == (yyyGNT **) NULL) 
      yyyfatal("malloc error in ox child list space allocation\n");
   (*yyyOxStackItem)->node->refCountListLen = yyyNattrbs; 
   (*yyyOxStackItem)->node->refCountList =  
            (yyyRCT *) calloc((size_t)yyyNattrbs, (size_t)sizeof(yyyRCT));  
   if ((*yyyOxStackItem)->node->refCountList == (yyyRCT *) NULL) 
      yyyfatal("malloc error in ox reference count list space allocation\n");  
   (*yyyOxStackItem)->node->prodNum = yyyProdNum; 
   va_start(ap, yyval_OxAttrbs); 
   for (i=1;i<=yyyRHSlength;i++) 
     {yyySIT *yaccStDum = va_arg(ap,struct yyyOxAttrbs *)->yyyOxStackItem;
      gnpDum = (*yyyOxStackItem)->node->cL[i-1] = yaccStDum->node;  
      gnpDum->whichSym = i;  
      gnpDum->parent.noderef = (*yyyOxStackItem)->node; 
      gnpDum->parentIsStack = 0;  
     } 
   va_end(ap); 
  } 



#define yyyDECORfREQ 50 



void yyyAdjustINRC(long yyyProdNum, int yyyRHSlength, long startP, long stopP, struct yyyOxAttrbs *yyval_OxAttrbs, ...) 
  {yyyWST i;
   yyySIT *yyyOxStackItem = yyval_OxAttrbs->yyyOxStackItem;
   long SSALptr,SSALptrHead,*cPtrPtr; 
   long *pL; 
   yyyGNT *gnpDum; 
   long iTemp;
   long nextP;
   static unsigned short intNodeCount = yyyDECORfREQ;
   va_list ap; 

   nextP = startP;
   while (nextP < stopP) 
     {if (yyyRCIL[nextP] == yyyR)  
         {(yyyOxStackItem->node->refCountList)[yyyRCIL[nextP+1]] = yyyRCIL[nextP+2];
         } 
         else 
         {(((yyyOxStackItem->node->cL)[yyyRCIL[nextP]])->refCountList)[yyyRCIL[nextP+1]] = yyyRCIL[nextP+2];
         } 
      nextP += 3;  
     }
   pL = yyyIIEL + yyyIIIEL[yyyProdNum]; 
   va_start(ap, yyval_OxAttrbs); 
   for (i=1;i<=yyyRHSlength;i++) 
     {yyySIT *yaccStDum = va_arg(ap,struct yyyOxAttrbs *)->yyyOxStackItem;
      pL++; 
      SSALptrHead = SSALptr = *(cPtrPtr = &(yaccStDum->solvedSAlist)); 
      if (SSALptr != yyyLambdaSSAL) 
         {*cPtrPtr = yyyLambdaSSAL; 
          do 
            {
             iTemp = (*pL+yyySSALspace[SSALptr].attrbNum);
             yyySignalEnts(yyyOxStackItem->node,
                           yyyIEL[iTemp],
                           yyyIEL[iTemp+1]
                          );  
             SSALptr = *(cPtrPtr = &(yyySSALspace[SSALptr].next)); 
            } 
            while (SSALptr != yyyLambdaSSAL);  
          *cPtrPtr = yyySSALCfreeList;  
          yyySSALCfreeList = SSALptrHead;  
         } 
     } 
   va_end(ap); 
   nextP = startP + 2;
   while (nextP < stopP) 
     {if (!yyyRCIL[nextP])
         {if (yyyRCIL[nextP-2] == yyyR)  
             {pL = &(yyyOxStackItem->solvedSAlist); 
              if (yyySSALCfreeList == yyyLambdaSSAL) 
                 {yyySSALspace[yyyNewSSALC].next = *pL; 
                  if ((*pL = yyyNewSSALC++) == yyySSALspaceSize) 
                     yyyHandleOverflow(yyySSALof); 
                 }  
                 else
                 {iTemp = yyySSALCfreeList; 
                  yyySSALCfreeList = yyySSALspace[yyySSALCfreeList].next; 
                  yyySSALspace[iTemp].next = *pL; 
                  *pL = iTemp;  
                 } 
              yyySSALspace[*pL].attrbNum = yyyRCIL[nextP-1]; 
             } 
             else 
             {if ((gnpDum = (yyyOxStackItem->node->cL)[yyyRCIL[nextP-2]])->prodNum != 0)
                 {
                  iTemp = yyyIIEL[yyyIIIEL[gnpDum->prodNum]] + yyyRCIL[nextP-1];
                  yyySignalEnts(gnpDum, 
                                yyyIEL[iTemp],  
                                yyyIEL[iTemp+1] 
                               );    
                 }  
             } 
         } 
      nextP += 3; 
     } 
   if (!--intNodeCount) 
      {intNodeCount = yyyDECORfREQ; 
       yyyDecorate(); 
      } 
  } 



void yyyGenLeaf(int nAttrbs,int typeNum,long startP,long stopP,YYSTYPE *yylval) 
  {yyyRCT *rcPdum; 
   yyySIT **yyyOxStackItem = &yylval->yyyOxAttrbs.yyyOxStackItem; 
   (*yyyOxStackItem) = (yyySIT *) malloc((size_t)sizeof(yyySIT)); 
   if ((*yyyOxStackItem) == (yyySIT *) NULL) 
      yyyfatal("malloc error in ox yacc semantic stack space allocation\n");
   (*yyyOxStackItem)->node = 
                          (yyyGNT *) malloc((size_t)sizeof(yyyGNT)) 
                         ; 
   if ((*yyyOxStackItem)->node == (yyyGNT *) NULL) 
      yyyfatal("malloc error in ox node space allocation\n");
   (*yyyOxStackItem)->solvedSAlist = yyyLambdaSSAL; 
   (*yyyOxStackItem)->node->parent.stackref = *yyyOxStackItem; 
   (*yyyOxStackItem)->node->parentIsStack = 1; 
   (*yyyOxStackItem)->node->cLlen = 0; 
   (*yyyOxStackItem)->node->cL = (yyyGNT **)NULL;  
   (*yyyOxStackItem)->node->refCountListLen = nAttrbs; 
   rcPdum = (*yyyOxStackItem)->node->refCountList =  
            (yyyRCT *) calloc((size_t)nAttrbs, (size_t)sizeof(yyyRCT));  
   if (rcPdum == (yyyRCT *) NULL) 
      yyyfatal("malloc error in ox reference count list space allocation\n");  
   while (startP < stopP) rcPdum[yyyLRCIL[startP++]] = 0; 
   (*yyyOxStackItem)->node->prodNum = 0; 
   (*yyyOxStackItem)->node->whichSym = 0; 
  } 



void yyyabort(void)
  {yyyYok = 0; 
  } 





#define yyyLastProdNum 8


#define yyyNsorts 1


int yyyProdsInd[] = {
   0,
   0,   2,   6,  10,  14,  18,  22,  24,
  26,
};


int yyyProds[][2] = {
{ 116,   0},{ 462,   0},{ 462,   0},{ 462,   0},{ 412,   0},
{ 462,   0},{ 462,   0},{ 462,   0},{ 420,   0},{ 462,   0},
{ 462,   0},{ 462,   0},{ 452,   0},{ 462,   0},{ 462,   0},
{ 462,   0},{ 436,   0},{ 462,   0},{ 462,   0},{ 396,   0},
{ 462,   0},{ 404,   0},{ 462,   0},{ 619,   1},{ 462,   0},
{ 567,   1},
};


int yyySortsInd[] = {
  0,
  0,
  1,
};


int yyySorts[] = {
  413,
};



char *yyyStringTab[] = {
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,"s",0,0,0,
0,0,"y",0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,"LRpre",0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,"'('",0,0,0,
0,0,0,0,"')'",
0,0,0,0,0,
0,0,"'*'","lexeme",0,
0,0,0,0,0,
"'+'",0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,"'-'",0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,"'/'",0,0,
0,0,0,0,0,
0,0,"expr",0,0,
0,0,0,0,0,
0,0,0,0,0,
0,"printf",0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,"CONST","LRpost",0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,"ID",
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,0,0,0,0,
0,
};



#define yyySizeofProd(num) (yyyProdsInd[(num)+1] - yyyProdsInd[(num)])

#define yyyGSoccurStr(prodNum,symPos) \
   (yyyStringTab[yyyProds[yyyProdsInd[(prodNum)] + (symPos)][0]])

#define yyySizeofSort(num) (yyySortsInd[(num)+1] - yyySortsInd[(num)])

#define yyySortOf(prodNum,symPos) \
  (yyyProds[yyyProdsInd[(prodNum)] + (symPos)][1]) 

#define yyyAttrbStr(prodNum,symPos,attrbNum)                      \
  (yyyStringTab[yyySorts[yyySortsInd[yyySortOf(prodNum,symPos)] + \
                         (attrbNum)                               \
                        ]                                         \
               ]                                                  \
  )



void yyyShowProd(int i)
  {int j,nSyms;

   nSyms = yyySizeofProd(i);
   for (j=0; j<nSyms; j++)
     {
      fprintf(stderr,"%s",yyyGSoccurStr(i,j));
      if (j == 0) fputs(" : ",stderr); else putc(' ',stderr);
     }
   fputs(";\n",stderr);
  }



void yyyShowProds()
  {int i; for (i=1; i<=yyyLastProdNum; i++) yyyShowProd(i);}



void yyyShowSymsAndSorts()
  {int i; 

   for (i=1; i<=yyyLastProdNum; i++) 
     {int j, nSyms;

      fprintf(stderr,
              "\n\n\n---------------------------------- %3.1d\n",i);
      /* yyyShowProd(i); */ 
      nSyms = yyySizeofProd(i); 
      for (j=0; j<nSyms; j++) 
        {int k, sortSize;

         fprintf(stderr,"%s\n",yyyGSoccurStr(i,j));
         sortSize = yyySizeofSort(yyySortOf(i,j));
         for (k=0; k<sortSize; k++) 
            fprintf(stderr,"  %s\n",yyyAttrbStr(i,j,k));
         if (j == 0) fputs("->\n",stderr); 
              else 
              putc('\n',stderr); 
        }
     }
  }



void yyyCheckNodeInstancesSolved(yyyGNT *np)
  {int mysort,sortSize,i,prodNum,symPos,inTerminalNode;
   int nUnsolvedInsts = 0;

   if (np->prodNum != 0)
     {inTerminalNode = 0;
      prodNum = np->prodNum;
      symPos = 0;
     }
   else
     {inTerminalNode = 1;
      prodNum = np->parent.noderef->prodNum;
      symPos = np->whichSym;
     }
   mysort = yyySortOf(prodNum,symPos);
   sortSize = yyySizeofSort(mysort);
   for (i=0; i<sortSize; i++)
     if ((np->refCountList)[i] != 0) nUnsolvedInsts += 1;
   if (nUnsolvedInsts)
     {fprintf(stderr,
      "\nFound node that has %d unsolved attribute instance(s).\n",
              nUnsolvedInsts
             );
      fprintf(stderr,"Node is labeled \"%s\".\n",
             yyyGSoccurStr(prodNum,symPos));
      if (inTerminalNode)
        {fputs("Node is terminal.  Its parent production is:\n  ",stderr);
         yyyShowProd(prodNum);
        }
      else
        {fputs("Node is nonterminal.  ",stderr);
         if (!(np->parentIsStack))
           {fprintf(stderr,
                    "Node is %dth child in its parent production:\n  ",
                   np->whichSym
                  );
            yyyShowProd(np->parent.noderef->prodNum);
           }
         fputs("Node is on left hand side of this production:\n  ",stderr);
         yyyShowProd(np->prodNum);
        }
      fputs("The following instances are unsolved:\n",stderr);
      for (i=0; i<sortSize; i++)
        if ((np->refCountList)[i] != 0)
          fprintf(stderr,"     %-16s still has %1d dependencies.\n",
                  yyyAttrbStr(prodNum,symPos,i),(np->refCountList)[i]);
     }
  }



void yyyCheckUnsolvedInstTrav(yyyGNT *pNode,long *nNZrc,long *cycleSum)
  {yyyGNT **yyyCLpdum;
   yyyRCT *rcp;
   int i;
  
   /* visit the refCountList of each node in the tree, and sum the non-zero refCounts */ 
   rcp = pNode->refCountList; 
   i = pNode->refCountListLen; 
   while (i--) 
      if (*rcp++) {*cycleSum += *(rcp - 1); (*nNZrc)++;} 
   yyyCLpdum = pNode->cL;
   i = pNode->cLlen;
   while (i--)
     {
      yyyCheckUnsolvedInstTrav(*yyyCLpdum,nNZrc,cycleSum);
      yyyCLpdum++;
     }
  }



void yyyUnsolvedInstSearchTravAux(yyyGNT *pNode)
  {yyyGNT **yyyCLpdum;
   int i;
  
   yyyCheckNodeInstancesSolved(pNode); 
   yyyCLpdum = pNode->cL;
   i = pNode->cLlen;
   while (i--)
     {
      yyyUnsolvedInstSearchTravAux(*yyyCLpdum);
      yyyCLpdum++;
     }
  }



void yyyUnsolvedInstSearchTrav(yyyGNT *pNode)
  {yyyGNT **yyyCLpdum;
   int i;
  
   yyyCLpdum = pNode->cL;
   i = pNode->cLlen;
   while (i--)
     {
      yyyUnsolvedInstSearchTravAux(*yyyCLpdum);
      yyyCLpdum++;
     }
  }



