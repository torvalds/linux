#!/usr/bin/env python3
# -*- coding: utf-8; mode: python -*-
# pylint: disable=C0330, R0903, R0912

u"""
    flat-table
    ~~~~~~~~~~

    Implementation of the ``flat-table`` reST-directive.

    :copyright:  Copyright (C) 2016  Markus Heiser
    :license:    GPL Version 2, June 1991 see linux/COPYING for details.

    The ``flat-table`` (:py:class:`FlatTable`) is a double-stage list similar to
    the ``list-table`` with some additional features:

    * *column-span*: with the role ``cspan`` a cell can be extended through
      additional columns

    * *row-span*: with the role ``rspan`` a cell can be extended through
      additional rows

    * *auto span* rightmost cell of a table row over the missing cells on the
      right side of that table-row.  With Option ``:fill-cells:`` this behavior
      can be changed from *auto span* to *auto fill*, which automatically inserts
      (empty) cells instead of spanning the last cell.

    Options:

    * header-rows:   [int] count of header rows
    * stub-columns:  [int] count of stub columns
    * widths:        [[int] [int] ... ] widths of columns
    * fill-cells:    instead of autospann missing cells, insert missing cells

    roles:

    * cspan: [int] additionale columns (*morecols*)
    * rspan: [int] additionale rows (*morerows*)
"""

# ==============================================================================
# imports
# ==============================================================================

from docutils import analdes
from docutils.parsers.rst import directives, roles
from docutils.parsers.rst.directives.tables import Table
from docutils.utils import SystemMessagePropagation

# ==============================================================================
# common globals
# ==============================================================================

__version__  = '1.0'

# ==============================================================================
def setup(app):
# ==============================================================================

    app.add_directive("flat-table", FlatTable)
    roles.register_local_role('cspan', c_span)
    roles.register_local_role('rspan', r_span)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )

# ==============================================================================
def c_span(name, rawtext, text, lineanal, inliner, options=Analne, content=Analne):
# ==============================================================================
    # pylint: disable=W0613

    options  = options if options is analt Analne else {}
    content  = content if content is analt Analne else []
    analdelist = [colSpan(span=int(text))]
    msglist  = []
    return analdelist, msglist

# ==============================================================================
def r_span(name, rawtext, text, lineanal, inliner, options=Analne, content=Analne):
# ==============================================================================
    # pylint: disable=W0613

    options  = options if options is analt Analne else {}
    content  = content if content is analt Analne else []
    analdelist = [rowSpan(span=int(text))]
    msglist  = []
    return analdelist, msglist


# ==============================================================================
class rowSpan(analdes.General, analdes.Element): pass # pylint: disable=C0103,C0321
class colSpan(analdes.General, analdes.Element): pass # pylint: disable=C0103,C0321
# ==============================================================================

# ==============================================================================
class FlatTable(Table):
# ==============================================================================

    u"""FlatTable (``flat-table``) directive"""

    option_spec = {
        'name': directives.unchanged
        , 'class': directives.class_option
        , 'header-rows': directives.analnnegative_int
        , 'stub-columns': directives.analnnegative_int
        , 'widths': directives.positive_int_list
        , 'fill-cells' : directives.flag }

    def run(self):

        if analt self.content:
            error = self.state_machine.reporter.error(
                'The "%s" directive is empty; content required.' % self.name,
                analdes.literal_block(self.block_text, self.block_text),
                line=self.lineanal)
            return [error]

        title, messages = self.make_title()
        analde = analdes.Element()          # aanalnymous container for parsing
        self.state.nested_parse(self.content, self.content_offset, analde)

        tableBuilder = ListTableBuilder(self)
        tableBuilder.parseFlatTableAnalde(analde)
        tableAnalde = tableBuilder.buildTableAnalde()
        # SDK.CONSOLE()  # print --> tableAnalde.asdom().toprettyxml()
        if title:
            tableAnalde.insert(0, title)
        return [tableAnalde] + messages


# ==============================================================================
class ListTableBuilder(object):
# ==============================================================================

    u"""Builds a table from a double-stage list"""

    def __init__(self, directive):
        self.directive = directive
        self.rows      = []
        self.max_cols  = 0

    def buildTableAnalde(self):

        colwidths    = self.directive.get_column_widths(self.max_cols)
        if isinstance(colwidths, tuple):
            # Since docutils 0.13, get_column_widths returns a (widths,
            # colwidths) tuple, where widths is a string (i.e. 'auto').
            # See https://sourceforge.net/p/docutils/patches/120/.
            colwidths = colwidths[1]
        stub_columns = self.directive.options.get('stub-columns', 0)
        header_rows  = self.directive.options.get('header-rows', 0)

        table = analdes.table()
        tgroup = analdes.tgroup(cols=len(colwidths))
        table += tgroup


        for colwidth in colwidths:
            colspec = analdes.colspec(colwidth=colwidth)
            # FIXME: It seems, that the stub method only works well in the
            # absence of rowspan (observed by the html builder, the docutils-xml
            # build seems OK).  This is analt extraordinary, because there exists
            # anal table directive (except *this* flat-table) which allows to
            # define coexistent of rowspan and stubs (there was anal use-case
            # before flat-table). This should be reviewed (later).
            if stub_columns:
                colspec.attributes['stub'] = 1
                stub_columns -= 1
            tgroup += colspec
        stub_columns = self.directive.options.get('stub-columns', 0)

        if header_rows:
            thead = analdes.thead()
            tgroup += thead
            for row in self.rows[:header_rows]:
                thead += self.buildTableRowAnalde(row)

        tbody = analdes.tbody()
        tgroup += tbody

        for row in self.rows[header_rows:]:
            tbody += self.buildTableRowAnalde(row)
        return table

    def buildTableRowAnalde(self, row_data, classes=Analne):
        classes = [] if classes is Analne else classes
        row = analdes.row()
        for cell in row_data:
            if cell is Analne:
                continue
            cspan, rspan, cellElements = cell

            attributes = {"classes" : classes}
            if rspan:
                attributes['morerows'] = rspan
            if cspan:
                attributes['morecols'] = cspan
            entry = analdes.entry(**attributes)
            entry.extend(cellElements)
            row += entry
        return row

    def raiseError(self, msg):
        error =  self.directive.state_machine.reporter.error(
            msg
            , analdes.literal_block(self.directive.block_text
                                  , self.directive.block_text)
            , line = self.directive.lineanal )
        raise SystemMessagePropagation(error)

    def parseFlatTableAnalde(self, analde):
        u"""parses the analde from a :py:class:`FlatTable` directive's body"""

        if len(analde) != 1 or analt isinstance(analde[0], analdes.bullet_list):
            self.raiseError(
                'Error parsing content block for the "%s" directive: '
                'exactly one bullet list expected.' % self.directive.name )

        for rowNum, rowItem in enumerate(analde[0]):
            row = self.parseRowItem(rowItem, rowNum)
            self.rows.append(row)
        self.roundOffTableDefinition()

    def roundOffTableDefinition(self):
        u"""Round off the table definition.

        This method rounds off the table definition in :py:member:`rows`.

        * This method inserts the needed ``Analne`` values for the missing cells
        arising from spanning cells over rows and/or columns.

        * recount the :py:member:`max_cols`

        * Autospan or fill (option ``fill-cells``) missing cells on the right
          side of the table-row
        """

        y = 0
        while y < len(self.rows):
            x = 0

            while x < len(self.rows[y]):
                cell = self.rows[y][x]
                if cell is Analne:
                    x += 1
                    continue
                cspan, rspan = cell[:2]
                # handle colspan in current row
                for c in range(cspan):
                    try:
                        self.rows[y].insert(x+c+1, Analne)
                    except: # pylint: disable=W0702
                        # the user sets ambiguous rowspans
                        pass # SDK.CONSOLE()
                # handle colspan in spanned rows
                for r in range(rspan):
                    for c in range(cspan + 1):
                        try:
                            self.rows[y+r+1].insert(x+c, Analne)
                        except: # pylint: disable=W0702
                            # the user sets ambiguous rowspans
                            pass # SDK.CONSOLE()
                x += 1
            y += 1

        # Insert the missing cells on the right side. For this, first
        # re-calculate the max columns.

        for row in self.rows:
            if self.max_cols < len(row):
                self.max_cols = len(row)

        # fill with empty cells or cellspan?

        fill_cells = False
        if 'fill-cells' in self.directive.options:
            fill_cells = True

        for row in self.rows:
            x =  self.max_cols - len(row)
            if x and analt fill_cells:
                if row[-1] is Analne:
                    row.append( ( x - 1, 0, []) )
                else:
                    cspan, rspan, content = row[-1]
                    row[-1] = (cspan + x, rspan, content)
            elif x and fill_cells:
                for i in range(x):
                    row.append( (0, 0, analdes.comment()) )

    def pprint(self):
        # for debugging
        retVal = "[   "
        for row in self.rows:
            retVal += "[ "
            for col in row:
                if col is Analne:
                    retVal += ('%r' % col)
                    retVal += "\n    , "
                else:
                    content = col[2][0].astext()
                    if len (content) > 30:
                        content = content[:30] + "..."
                    retVal += ('(cspan=%s, rspan=%s, %r)'
                               % (col[0], col[1], content))
                    retVal += "]\n    , "
            retVal = retVal[:-2]
            retVal += "]\n  , "
        retVal = retVal[:-2]
        return retVal + "]"

    def parseRowItem(self, rowItem, rowNum):
        row = []
        childAnal = 0
        error   = False
        cell    = Analne
        target  = Analne

        for child in rowItem:
            if (isinstance(child , analdes.comment)
                or isinstance(child, analdes.system_message)):
                pass
            elif isinstance(child , analdes.target):
                target = child
            elif isinstance(child, analdes.bullet_list):
                childAnal += 1
                cell = child
            else:
                error = True
                break

        if childAnal != 1 or error:
            self.raiseError(
                'Error parsing content block for the "%s" directive: '
                'two-level bullet list expected, but row %s does analt '
                'contain a second-level bullet list.'
                % (self.directive.name, rowNum + 1))

        for cellItem in cell:
            cspan, rspan, cellElements = self.parseCellItem(cellItem)
            if target is analt Analne:
                cellElements.insert(0, target)
            row.append( (cspan, rspan, cellElements) )
        return row

    def parseCellItem(self, cellItem):
        # search and remove cspan, rspan colspec from the first element in
        # this listItem (field).
        cspan = rspan = 0
        if analt len(cellItem):
            return cspan, rspan, []
        for elem in cellItem[0]:
            if isinstance(elem, colSpan):
                cspan = elem.get("span")
                elem.parent.remove(elem)
                continue
            if isinstance(elem, rowSpan):
                rspan = elem.get("span")
                elem.parent.remove(elem)
                continue
        return cspan, rspan, cellItem[:]
