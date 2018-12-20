# SPDX-License-Identifier: GPL-2.0+
# vim: ts=2:sw=2:et:tw=80:nowrap

import os, csv, glob

class CSVCollection(dict):
  delimiter=';'
  quotechar='"'
  source_column_name = 'Sources / Destinations'

  """
  This class is a dictionary representation of the collection of sheets that
  exist in a given .ODS file.
  """
  def __init__(self, pattern, skip_commented_lines=True, strip_lines=True):
    super(CSVCollection, self).__init__()
    self.pattern = pattern
    C = '#' if skip_commented_lines else 'blahblahblah'

    if strip_lines:
      strip = lambda s:s.strip()
    else:
      strip = lambda s:s

    # load all CSV files
    key = self.source_column_name
    for fname in glob.glob(pattern):
      with open(fname) as F:
        dR = csv.DictReader(F, delimiter=self.delimiter,
                            quotechar=self.quotechar)
        name = os.path.basename(fname).partition('.')[0]
        D = {
          r[key]:{f:strip(c) for f,c in r.items()
                  if f != key and f[:1] not in ['', C] and
                     strip(c)[:1] not in ['', C]}
          for r in dR if r[key][:1] not in ['', C]
        }
        # now, go back through and eliminate all empty dictionaries
        D = {k:v for k,v in D.items() if v}
        self[name] = D
