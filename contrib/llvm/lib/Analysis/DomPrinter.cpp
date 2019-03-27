//===- DomPrinter.cpp - DOT printer for the dominance trees    ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines '-dot-dom' and '-dot-postdom' analysis passes, which emit
// a dom.<fnname>.dot or postdom.<fnname>.dot file for each function in the
// program, with a graph of the dominance/postdominance tree of that
// function.
//
// There are also passes available to directly call dotty ('-view-dom' or
// '-view-postdom'). By appending '-only' like '-dot-dom-only' only the
// names of the bbs are printed, but the content is hidden.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DomPrinter.h"
#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/PostDominators.h"

using namespace llvm;

namespace llvm {
template<>
struct DOTGraphTraits<DomTreeNode*> : public DefaultDOTGraphTraits {

  DOTGraphTraits (bool isSimple=false)
    : DefaultDOTGraphTraits(isSimple) {}

  std::string getNodeLabel(DomTreeNode *Node, DomTreeNode *Graph) {

    BasicBlock *BB = Node->getBlock();

    if (!BB)
      return "Post dominance root node";


    if (isSimple())
      return DOTGraphTraits<const Function*>
        ::getSimpleNodeLabel(BB, BB->getParent());
    else
      return DOTGraphTraits<const Function*>
        ::getCompleteNodeLabel(BB, BB->getParent());
  }
};

template<>
struct DOTGraphTraits<DominatorTree*> : public DOTGraphTraits<DomTreeNode*> {

  DOTGraphTraits (bool isSimple=false)
    : DOTGraphTraits<DomTreeNode*>(isSimple) {}

  static std::string getGraphName(DominatorTree *DT) {
    return "Dominator tree";
  }

  std::string getNodeLabel(DomTreeNode *Node, DominatorTree *G) {
    return DOTGraphTraits<DomTreeNode*>::getNodeLabel(Node, G->getRootNode());
  }
};

template<>
struct DOTGraphTraits<PostDominatorTree*>
  : public DOTGraphTraits<DomTreeNode*> {

  DOTGraphTraits (bool isSimple=false)
    : DOTGraphTraits<DomTreeNode*>(isSimple) {}

  static std::string getGraphName(PostDominatorTree *DT) {
    return "Post dominator tree";
  }

  std::string getNodeLabel(DomTreeNode *Node, PostDominatorTree *G ) {
    return DOTGraphTraits<DomTreeNode*>::getNodeLabel(Node, G->getRootNode());
  }
};
}

void DominatorTree::viewGraph(const Twine &Name, const Twine &Title) {
#ifndef NDEBUG
  ViewGraph(this, Name, false, Title);
#else
  errs() << "DomTree dump not available, build with DEBUG\n";
#endif  // NDEBUG
}

void DominatorTree::viewGraph() {
#ifndef NDEBUG
  this->viewGraph("domtree", "Dominator Tree for function");
#else
  errs() << "DomTree dump not available, build with DEBUG\n";
#endif  // NDEBUG
}

namespace {
struct DominatorTreeWrapperPassAnalysisGraphTraits {
  static DominatorTree *getGraph(DominatorTreeWrapperPass *DTWP) {
    return &DTWP->getDomTree();
  }
};

struct DomViewer : public DOTGraphTraitsViewer<
                       DominatorTreeWrapperPass, false, DominatorTree *,
                       DominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  DomViewer()
      : DOTGraphTraitsViewer<DominatorTreeWrapperPass, false, DominatorTree *,
                             DominatorTreeWrapperPassAnalysisGraphTraits>(
            "dom", ID) {
    initializeDomViewerPass(*PassRegistry::getPassRegistry());
  }
};

struct DomOnlyViewer : public DOTGraphTraitsViewer<
                           DominatorTreeWrapperPass, true, DominatorTree *,
                           DominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  DomOnlyViewer()
      : DOTGraphTraitsViewer<DominatorTreeWrapperPass, true, DominatorTree *,
                             DominatorTreeWrapperPassAnalysisGraphTraits>(
            "domonly", ID) {
    initializeDomOnlyViewerPass(*PassRegistry::getPassRegistry());
  }
};

struct PostDominatorTreeWrapperPassAnalysisGraphTraits {
  static PostDominatorTree *getGraph(PostDominatorTreeWrapperPass *PDTWP) {
    return &PDTWP->getPostDomTree();
  }
};

struct PostDomViewer : public DOTGraphTraitsViewer<
                          PostDominatorTreeWrapperPass, false,
                          PostDominatorTree *,
                          PostDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  PostDomViewer() :
    DOTGraphTraitsViewer<PostDominatorTreeWrapperPass, false,
                         PostDominatorTree *,
                         PostDominatorTreeWrapperPassAnalysisGraphTraits>(
        "postdom", ID){
      initializePostDomViewerPass(*PassRegistry::getPassRegistry());
    }
};

struct PostDomOnlyViewer : public DOTGraphTraitsViewer<
                            PostDominatorTreeWrapperPass, true,
                            PostDominatorTree *,
                            PostDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  PostDomOnlyViewer() :
    DOTGraphTraitsViewer<PostDominatorTreeWrapperPass, true,
                         PostDominatorTree *,
                         PostDominatorTreeWrapperPassAnalysisGraphTraits>(
        "postdomonly", ID){
      initializePostDomOnlyViewerPass(*PassRegistry::getPassRegistry());
    }
};
} // end anonymous namespace

char DomViewer::ID = 0;
INITIALIZE_PASS(DomViewer, "view-dom",
                "View dominance tree of function", false, false)

char DomOnlyViewer::ID = 0;
INITIALIZE_PASS(DomOnlyViewer, "view-dom-only",
                "View dominance tree of function (with no function bodies)",
                false, false)

char PostDomViewer::ID = 0;
INITIALIZE_PASS(PostDomViewer, "view-postdom",
                "View postdominance tree of function", false, false)

char PostDomOnlyViewer::ID = 0;
INITIALIZE_PASS(PostDomOnlyViewer, "view-postdom-only",
                "View postdominance tree of function "
                "(with no function bodies)",
                false, false)

namespace {
struct DomPrinter : public DOTGraphTraitsPrinter<
                        DominatorTreeWrapperPass, false, DominatorTree *,
                        DominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  DomPrinter()
      : DOTGraphTraitsPrinter<DominatorTreeWrapperPass, false, DominatorTree *,
                              DominatorTreeWrapperPassAnalysisGraphTraits>(
            "dom", ID) {
    initializeDomPrinterPass(*PassRegistry::getPassRegistry());
  }
};

struct DomOnlyPrinter : public DOTGraphTraitsPrinter<
                            DominatorTreeWrapperPass, true, DominatorTree *,
                            DominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  DomOnlyPrinter()
      : DOTGraphTraitsPrinter<DominatorTreeWrapperPass, true, DominatorTree *,
                              DominatorTreeWrapperPassAnalysisGraphTraits>(
            "domonly", ID) {
    initializeDomOnlyPrinterPass(*PassRegistry::getPassRegistry());
  }
};

struct PostDomPrinter
  : public DOTGraphTraitsPrinter<
                            PostDominatorTreeWrapperPass, false,
                            PostDominatorTree *,
                            PostDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  PostDomPrinter() :
    DOTGraphTraitsPrinter<PostDominatorTreeWrapperPass, false,
                          PostDominatorTree *,
                          PostDominatorTreeWrapperPassAnalysisGraphTraits>(
        "postdom", ID) {
      initializePostDomPrinterPass(*PassRegistry::getPassRegistry());
    }
};

struct PostDomOnlyPrinter
  : public DOTGraphTraitsPrinter<
                            PostDominatorTreeWrapperPass, true,
                            PostDominatorTree *,
                            PostDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  PostDomOnlyPrinter() :
    DOTGraphTraitsPrinter<PostDominatorTreeWrapperPass, true,
                          PostDominatorTree *,
                          PostDominatorTreeWrapperPassAnalysisGraphTraits>(
        "postdomonly", ID) {
      initializePostDomOnlyPrinterPass(*PassRegistry::getPassRegistry());
    }
};
} // end anonymous namespace



char DomPrinter::ID = 0;
INITIALIZE_PASS(DomPrinter, "dot-dom",
                "Print dominance tree of function to 'dot' file",
                false, false)

char DomOnlyPrinter::ID = 0;
INITIALIZE_PASS(DomOnlyPrinter, "dot-dom-only",
                "Print dominance tree of function to 'dot' file "
                "(with no function bodies)",
                false, false)

char PostDomPrinter::ID = 0;
INITIALIZE_PASS(PostDomPrinter, "dot-postdom",
                "Print postdominance tree of function to 'dot' file",
                false, false)

char PostDomOnlyPrinter::ID = 0;
INITIALIZE_PASS(PostDomOnlyPrinter, "dot-postdom-only",
                "Print postdominance tree of function to 'dot' file "
                "(with no function bodies)",
                false, false)

// Create methods available outside of this file, to use them
// "include/llvm/LinkAllPasses.h". Otherwise the pass would be deleted by
// the link time optimization.

FunctionPass *llvm::createDomPrinterPass() {
  return new DomPrinter();
}

FunctionPass *llvm::createDomOnlyPrinterPass() {
  return new DomOnlyPrinter();
}

FunctionPass *llvm::createDomViewerPass() {
  return new DomViewer();
}

FunctionPass *llvm::createDomOnlyViewerPass() {
  return new DomOnlyViewer();
}

FunctionPass *llvm::createPostDomPrinterPass() {
  return new PostDomPrinter();
}

FunctionPass *llvm::createPostDomOnlyPrinterPass() {
  return new PostDomOnlyPrinter();
}

FunctionPass *llvm::createPostDomViewerPass() {
  return new PostDomViewer();
}

FunctionPass *llvm::createPostDomOnlyViewerPass() {
  return new PostDomOnlyViewer();
}
