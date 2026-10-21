.. SPDX-License-Identifier: GPL-2.0

====================================
Backporting e resolução de conflitos
====================================

:Autor: Vegard Nossum <vegard.nossum@oracle.com>

.. contents::
    :local:
    :depth: 3
    :backlinks: none

Introdução
==========

Alguns desenvolvedores podem nunca precisar lidar de fato com backporting de
patches, mesclagem de ramificações (branches) ou resolução de conflitos em seu
trabalho diário, portanto, quando um conflito de mesclagem aparece, pode ser
assustador. Felizmente, resolver conflitos é uma habilidade como qualquer outra,
e existem muitas técnicas úteis que você pode usar para tornar o processo mais
suave e aumentar sua confiança no resultado.

Este documento tem como objetivo ser um guia abrangente e passo a passo para
backporting e resolução de conflitos.

Aplicando o patch a uma árvore
==============================

Às vezes, o patch que você está fazendo backport já existe como um commit do
git, caso em que você apenas faz o cherry-pick dele diretamente usando
``git cherry-pick``. No entanto, se o patch vier de um e-mail, como costuma
acontecer no caso do kernel Linux, você precisará aplicá-lo a uma árvore usando
``git am``.

Se você já usou o ``git am``, provavelmente já sabe que ele é bastante exigente
sobre o patch ser aplicado perfeitamente à sua árvore de código-fonte. Na
verdade, você provavelmente já teve pesadelos com arquivos ``.rej`` e tentando
editar o patch para fazê-lo ser aplicado.

Recomenda-se fortemente, em vez disso, encontrar uma versão base apropriada onde
o patch se aplique de forma limpa e *então* fazer o cherry-pick dele para a sua
árvore de destino, pois isso fará com que o git exiba marcadores de conflito e
permitirá que você resolva os conflitos com a ajuda do git e de quaisquer outras
ferramentas de resolução de conflitos que preferir usar. Por exemplo, se você
quiser aplicar um patch que acabou de chegar na LKML a um kernel estável mais
antigo, você pode aplicá-lo ao kernel principal (mainline) mais recente e, em
seguida, fazer o cherry-pick dele para a sua ramificação estável mais antiga.

Geralmente é melhor usar exatamente a mesma base a partir da qual o patch foi
gerado, mas isso não importa tanto, desde que ele se aplique de forma limpa e
não esteja muito longe da base original. O único problema ao aplicar o patch na
base "errada" é que isso pode trazer mais alterações não relacionadas no
contexto do diff ao fazer o cherry-pick dele para a ramificação mais antiga.

Um bom motivo para preferir o ``git cherry-pick`` em vez do ``git am`` é que o
git conhece o histórico preciso de um commit existente, de modo que ele saberá
quando o código foi movido de lugar e teve seus números de linha alterados; isso,
por sua vez, torna menos provável que o patch seja aplicado no lugar errado (o
que pode resultar em erros silenciosos ou conflitos confusos).

Se você estiver usando o `b4`_. e estiver aplicando o patch diretamente de um
e-mail, você pode usar o ``b4 am`` com as opções ``-g``/``--guess-base`` e
``-3``/``--prep-3way`` para fazer parte disso automaticamente (veja a
`apresentação do b4`_ para mais informações). No entanto, o restante deste
artigo assumirá que você está fazendo um ``git cherry-pick`` simples.

.. _b4: https://people.kernel.org/monsieuricon/introducing-b4-and-patch-attestation
.. _apresentação do b4: https://youtu.be/mF10hgVIx9o?t=2996

Assim que tiver o patch no git, você pode prosseguir e fazer o cherry-pick dele
em sua árvore de código-fonte. Não se esqueça de fazer o cherry-pick com ``-x``
se quiser um registro por escrito de onde o patch veio!

Note que, se você estiver enviando um patch para a ramificação estável (stable),
o formato é ligeiramente diferente; a primeira linha após a linha de assunto
precisa ser::

    commit <upstream commit> upstream

ou::

    [ Upstream commit <upstream commit> ]

Resolvendo conflitos
====================

Ih, rapaz; o cherry-pick falhou com uma mensagem vagamente ameaçadora::

    CONFLICT (content): Merge conflict

O que fazer agora?

Em geral, os conflitos aparecem quando o contexto do patch (ou seja, as linhas
que estão sendo alteradas e/ou as linhas que cercam as alterações) não
corresponde ao que está na árvore à qual você está tentando aplicar o patch.

No caso de backports, o que provavelmente aconteceu foi que a ramificação
(branch) a partir da qual você está fazendo o backport contém patches que não
estão na ramificação para a qual você está fazendo o backport. No entanto, o
inverso também é possível. Em qualquer caso, o resultado é um conflito que
precisa ser resolvido.

Se a sua tentativa de cherry-pick falhar com um conflito, o git edita os
arquivos automaticamente para incluir os chamados marcadores de conflito,
mostrando onde está o conflito e como as duas ramificações divergiram. Resolver
o conflito normalmente significa editar o resultado final de forma que ele leve
em consideração esses outros commits.

A resolução do conflito pode ser feita manualmente em um editor de texto comum
ou usando uma ferramenta dedicada de resolução de conflitos.

Muitas pessoas preferem usar seu editor de texto comum e editar o conflito
diretamente, pois pode ser mais fácil entender o que você está fazendo e
controlar o resultado final. Definitivamente, existem prós e contras em cada
método, e às vezes há valor em usar ambos.

Não abordaremos o uso de ferramentas de mesclagem (merge tools) dedicadas aqui,
além de fornecer algumas indicações de várias ferramentas que você poderia usar:

-  `Modo Emacs Ediff <https://www.emacswiki.org/emacs/EdiffMode>`__
-  `vimdiff/gvimdiff <https://linux.die.net/man/1/vimdiff>`__
-  `KDiff3 <http://kdiff3.sourceforge.net/>`__
-  `TortoiseMerge <https://tortoisesvn.net/TortoiseMerge.html>`__
-  `Meld <https://meldmerge.org/help/>`__
-  `P4Merge <https://www.perforce.com/products/helix-core-apps/merge-diff-tool-p4merge>`__
-  `Beyond Compare <https://www.scootersoftware.com/>`__
-  `IntelliJ <https://www.jetbrains.com/help/idea/resolve-conflicts.html>`__
-  `VSCode <https://code.visualstudio.com/docs/editor/versioncontrol>`__

Para configurar o git para funcionar com elas, veja ``git mergetool --help`` ou
a `documentação oficial do git-mergetool`_.

.. _documentação oficial do git-mergetool: https://git-scm.com/docs/git-mergetool

Patches pré-requisitos
----------------------

A maioria dos conflitos acontece porque a ramificação para a qual você está
fazendo o backport não possui alguns patches em comparação com a ramificação a
partir da qual você está fazendo o backport. No caso mais geral (como a
mesclagem de duas ramificações independentes), o desenvolvimento poderia ter
ocorrido em qualquer uma das ramificações, ou as ramificações simplesmente
divergiram -- talvez a sua ramificação mais antiga tenha recebido alguns outros
backports que, por si só, precisaram de resoluções de conflitos, causando uma
divergência.

É importante sempre identificar o commit ou os commits que causaram o conflito,
pois, caso contrário, você não poderá ter confiança na correção da sua
resolução. Como um bônus adicional, especialmente se o patch for em uma área com
a qual você não está muito familiarizado, os registros de alterações (changelogs)
desses commits frequentemente lhe darão o contexto para entender o código e os
problemas ou armadilhas potenciais com a sua resolução de conflito.

git log
~~~~~~~

Um bom primeiro passo é olhar o ``git log`` para o arquivo que possui o
conflito -- isso geralmente é suficiente quando não há muitos patches no
arquivo, mas pode ficar confuso se o arquivo for grande e frequentemente
modificado por patches. Você deve executar o ``git log`` no intervalo de commits
entre a sua ramificação atualmente ativa (``HEAD``) e o pai do patch que você está
escolhendo (``<commit>``), ou seja::

    git log HEAD..<commit>^ -- <path>

Melhor ainda, se você quiser restringir essa saída a uma única função (porque é
onde o conflito aparece), você pode usar a seguinte sintaxe::

    git log -L:'\<function\>':<path> HEAD..<commit>^

.. note::
     O ``\<`` e o ``\>`` ao redor do nome da função garantem que as
     correspondências fiquem ancoradas em um limite de palavra. Isso é
     importante, pois essa parte é na verdade uma regex e o git segue apenas a
     primeira correspondência; portanto, se você usar
     ``-L:thread_stack:kernel/fork.c``, ele poderá fornecer apenas resultados
     para a função ``try_release_thread_stack_to_cache``, embora existam muitas
     outras funções naquele arquivo contendo a string ``thread_stack`` em seus
     nomes.

Outra opção útil para o ``git log`` é a ``-G``, que permite filtrar por certas
strings que aparecem nos diffs dos commits que você está listando::

    git log -G'regex' HEAD..<commit>^ -- <path>

Esta também pode ser uma maneira prática de encontrar rapidamente quando algo
(por exemplo, uma chamada de função ou uma variável) foi alterado, adicionado
ou removido. A string de busca é uma expressão regular, o que significa que você
pode potencialmente buscar por coisas mais específicas, como atribuições a um
membro específico de uma struct::

    git log -G'\->index\>.*='

git blame
~~~~~~~~~

Outra maneira de encontrar commits pré-requisitos (embora apenas o mais recente
para um determinado conflito) é executar o ``git blame``. Neste caso, você
precisa executá-lo no commit pai do patch para o qual está fazendo o
cherry-pick e no arquivo onde o conflito apareceu, ou seja::

    git blame <commit>^ -- <path>

Este comando também aceita o argumento ``-L`` (para restringir a saída a uma
única função), mas, neste caso, você especifica o nome do arquivo no final do
comando, como de costume::

    git blame -L:'\<function\>' <commit>^ -- <path>

Navegue até o local onde o conflito ocorreu. A primeira coluna da saída do
blame é o ID do commit do patch que adicionou uma determinada linha de código.

Pode ser uma boa ideia dar um ``git show`` nesses commits e ver se eles se
parecem com a possível origem do conflito. Às vezes, haverá mais de um desses
commits, seja porque múltiplos commits alteraram linhas diferentes da mesma área
de conflito *ou* porque múltiplos patches subsequentes alteraram a mesma linha
(ou linhas) várias vezes. Neste último caso, você pode ter que executar o
``git blame`` novamente e especificar a versão mais antiga do arquivo para
analisar, a fim de cavar mais fundo no histórico do arquivo.

Patches pré-requisitos vs. incidentais
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Tendo encontrado o patch que causou o conflito, você precisa determinar se ele
é um pré-requisito para o patch que você está fazendo o backport ou se é apenas
incidental e pode ser pulado. Um patch incidental seria aquele que toca no mesmo
código que o patch para o qual você está fazendo o backport, mas não altera a
semântica do código de nenhuma forma relevante. Por exemplo, um patch de limpeza
de espaços em branco é completamente incidental -- da mesma forma, um patch que
simplesmente renomeia uma função ou uma variável também seria incidental. Por
outro lado, se a função que está sendo alterada sequer existe na sua ramificação
atual, então isso não seria nada incidental e você precisa considerar com
cuidado se o patch que adiciona a função deve ser aplicado via cherry-pick
primeiro.

Se você descobrir que há um patch pré-requisito necessário, então você precisa
parar e fazer o cherry-pick dele em vez disso. Se você já resolveu alguns
conflitos em um arquivo diferente e não quer fazer isso de novo, você pode
criar uma cópia temporária daquele arquivo.

Para abortar o cherry-pick atual, vá em frente e execute
``git cherry-pick --abort`` e, em seguida, reinicie o processo de cherry-pick
com o ID do commit do patch pré-requisito.

Entendendo os marcadores de conflito
------------------------------------

Diffs combinados
~~~~~~~~~~~~~~~~

Digamos que você tenha decidido não fazer o cherry-pick (ou o revert) de patches
adicionais e quer apenas resolver o conflito. O Git terá inserido marcadores de
conflito no seu arquivo. Por padrão, isso se parecerá com algo como::

    <<<<<<< HEAD
    this is what's in your current tree before cherry-picking
    =======
    this is what the patch wants it to be after cherry-picking
    >>>>>>> <commit>... title

Isso é o que você veria se abrisse o arquivo no seu editor. No entanto, se você
executasse o ``git diff`` sem nenhum argumento, a saída seria algo assim::

    $ git diff
    [...]
    ++<<<<<<<< HEAD
     +this is what's in your current tree before cherry-picking
    ++========
    + this is what the patch wants it to be after cherry-picking
    ++>>>>>>>> <commit>... title

Quando você está resolvendo um conflito, o comportamento do ``git diff`` difere
do seu comportamento normal. Note as duas colunas de marcadores de diff em vez
da coluna única usual; este é o chamado "`diff combinado`_", aqui mostrando o
diff de 3 vias (ou diff-de-diffs) entre:

#. a ramificação atual (antes do cherry-pick) e o diretório de trabalho atual, e
#. a ramificação atual (antes do cherry-pick) e o arquivo como ele fica após o
   patch original ter sido aplicado.

.. _diff combinado: https://git-scm.com/docs/diff-format#_combined_diff_format

Diffs melhores
~~~~~~~~~~~~~~

Diffs combinados de 3 vias incluem todas as outras alterações que aconteceram
no arquivo entre a sua ramificação atual e a ramificação a partir da qual você
está fazendo o cherry-pick. Embora isso seja útil para detectar outras
alterações que você precisa levar em consideração, também torna a saída do
``git diff`` um tanto intimidadora e difícil de ler. Em vez disso, você pode
preferir executar ``git diff HEAD`` (ou ``git diff --ours``), que mostra apenas
o diff entre a ramificação atual antes do cherry-pick e o diretório de trabalho
atual. Ele se parece com isso::

    $ git diff HEAD
    [...]
    +<<<<<<<< HEAD
     this is what's in your current tree before cherry-picking
    +========
    +this is what the patch wants it to be after cherry-picking
    +>>>>>>>> <commit>... title

Como você pode ver, isso é lido exatamente como qualquer outro diff e deixa claro
quais linhas estão na ramificação atual e quais linhas estão sendo adicionadas
porque fazem parte do conflito de mesclagem ou do patch que está sendo aplicado
via cherry-pick.

Estilos de mesclagem e diff3
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

O estilo padrão de marcador de conflito mostrado acima é conhecido como o estilo
``merge``. Também está disponível um outro estilo, conhecido como o estilo
``diff3``, que se parece com isso::

    <<<<<<< HEAD
    this is what is in your current tree before cherry-picking
    ||||||| parent of <commit> (title)
    this is what the patch expected to find there
    =======
    this is what the patch wants it to be after being applied
    >>>>>>> <commit> (title)

Como você pode ver, isso tem 3 partes em vez de 2, e inclui o que o git
esperava encontrar lá, mas não encontrou. É *altamente recomendável* usar este
estilo de conflito, pois deixa muito mais claro o que o patch realmente alterou;
ou seja, ele permite que você compare as versões de antes e depois do arquivo
para o commit do qual está fazendo o cherry-pick. Isso permite que você tome
melhores decisões sobre como resolver o conflito.

Para alterar os estilos de marcadores de conflito, você pode usar o seguinte
comando::

    git config merge.conflictStyle diff3

Existe uma terceira opção, ``zdiff3``, introduzida no `Git 2.35`_, que possui as
mesmas 3 seções do ``diff3``, mas onde as linhas comuns foram cortadas, tornando
a área de conflito menor em alguns casos.

.. _Git 2.35: https://github.blog/2022-01-24-highlights-from-git-2-35/

Iterando em resoluções de conflito
----------------------------------

O primeiro passo em qualquer processo de resolução de conflito é entender o
patch para o qual você está fazendo o backport. Para o kernel Linux, isso é
especialmente importante, pois uma alteração incorreta pode levar ao travamento
de todo o sistema -- ou pior, a uma vulnerabilidade de segurança não detectada.

Entender o patch pode ser fácil ou difícil, dependendo do próprio patch, do
registro de alterações (changelog) e da sua familiaridade com o código que está
sendo alterado. No entanto, uma boa pergunta para cada alteração (ou cada bloco/
hunk do patch) seria: "Por que este hunk está no patch?" As respostas a essas
perguntas orientarão a sua resolução de conflito.

Processo de resolução
~~~~~~~~~~~~~~~~~~~~~

Às vezes, a coisa mais fácil a fazer é apenas remover tudo, exceto a primeira
parteda do conflito, deixando o arquivo essencialmente inalterado, e aplicar
as alterações manualmente. Talvez o patch esteja alterando um argumento de
chamada de função de ``0`` para ``1``, enquanto uma alteração conflitante
adicionou um parâmetro totalmente novo (e insignificante) ao final da lista de
parâmetros; nesse caso, é bastante fácil alterar o argumento de ``0`` para ``1``
manualmente e deixar o restante dos argumentos como estão. Esta técnica de
aplicar alterações manualmente é mais útil se o conflito tiver trazido muito
contexto não relacionado com o qual você não precisa realmente se preocupar.

Para conflitos particularmente difíceis com muitos marcadores de conflito, você
pode usar ``git add`` ou ``git add -i`` para indexar (stage) seletivamente as
suas resoluções para tirá-las do caminho; isso também permite que você use
``git diff HEAD`` para ver sempre o que ainda resta a ser resolvido ou
``git diff --cached`` para ver como está o seu patch até o momento.

Lidando com arquivos renomeados
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Uma das coisas mais irritantes que podem acontecer ao fazer o backport de um
patch é descobrir que um dos arquivos modificados foi renomeado, pois isso
geralmente significa que o git sequer colocará marcadores de conflito, mas
apenas lavará as mãos e dirá (parafraseando): "Caminho não mesclado! Faça você o
trabalho..."

Geralmente existem algumas maneiras de lidar com isso. Se o patch para o
arquivo renomeado for pequeno, como uma alteração de uma única linha, a coisa
mais fácil é prosseguir, aplicar a alteração manualmente e dar o caso por
encerrado. Por outro lado, se a alteração for grande ou complicada, você
definitivamente não vai querer fazê-la manualmente.

Como uma primeira tentativa, você pode tentar algo assim, que reduzirá o limite
(threshold) de detecção de renomeação para 30% (por padrão, o git usa 50%, o que
significa que dois arquivos precisam ter pelo menos 50% em comum para que ele
considere um par de adição/remoção como uma renomeação potencial)::

  git cherry-pick -strategy=recursive -Xrename-threshold=30

Às vezes, a coisa certa a fazer será fazer o backport também do patch que
realizou a renomeação, mas esse definitivamente não é o caso mais comum. Em vez
disso, o que você pode fazer é renomear temporariamente o arquivo na
ramificação para a qual está fazendo o backport (usando ``git mv`` e commitando
o resultado), reiniciar a tentativa de cherry-pick do patch, renomear o arquivo
de volta (``git mv`` e commitando novamente) e, finalmente, esmagar (squash) o
resultado usando ``git rebase -i`` (veja o `tutorial de rebase`_) para que ele
apareça como um único commit quando você terminar.

.. _tutorial de rebase: [https://medium.com/@slamflipstrom/a-beginners-guide-to-squashing-commits-with-git-rebase-8185cf6e62ec](https://medium.com/@slamflipstrom/a-beginners-guide-to-squashing-commits-with-git-rebase-8185cf6e62ec)

Pegadinhas
----------

Argumentos de função
~~~~~~~~~~~~~~~~~~~~

Preste atenção às alterações em argumentos de função! É fácil deixar passar
detalhes e pensar que duas linhas são iguais quando, na verdade, elas diferem em
algum pequeno detalhe, como qual variável foi passada como argumento
(especialmente se as duas variáveis forem de apenas um caractere e parecerem
iguais, como i e j).

Tratamento de erros
~~~~~~~~~~~~~~~~~~~

Se você fizer o cherry-pick de um patch que inclua uma instrução ``goto``
(geralmente para tratamento de erros), é absolutamente imperativo verificar em
dobro se o rótulo (label) de destino ainda está correto na ramificação para a
qual você está fazendo o backport. O mesmo vale para instruções ``return``,
``break`` e ``continue`` adicionadas.

O tratamento de erros geralmente fica localizado no final da função, portanto,
pode não fazer parte do conflito, mesmo que possa ter sido alterado por outros
patches.

Uma boa maneira de garantir que você revise os caminhos de erro é sempre usar
``git diff -W`` e ``git show -W`` (também conhecido como ``--function-context``)
ao inspecionar suas alterações. Para código em C, isso mostrará toda a função
que está sendo alterada em um patch. Uma das coisas que frequentemente dão
errado durante backports é que algo mais na função mudou em qualquer uma das
ramificações a partir da qual ou para a qual você está fazendo o backport. Ao
incluir a função inteira no diff, você obtém mais contexto e pode identificar
mais facilmente problemas que de outra forma poderiam passar despercebidos.

Código refatorado
~~~~~~~~~~~~~~~~~

Algo que acontece com bastante frequência é o código ser refatorado ao "isolar"
uma sequência ou padrão de código comum em uma função auxiliar. Ao fazer o
backport de patches para uma área onde tal refatoração ocorreu, você efetivamente
precisa fazer o inverso ao realizar o backport: um patch para um único local pode
precisar ser aplicado a múltiplos locais na versão que recebeu o backport. (Um
indicativo para este cenário é que uma função foi renomeada -- mas nem sempre é o
caso.)

Para evitar backports incompletos, vale a pena tentar descobrir se o patch
corrige um bug que aparece em mais de um lugar. Uma maneira de fazer isso seria
usar o ``git grep``. (Isso, na verdade, é uma boa ideia de se fazer em geral, não
apenas para backports.) Se você descobrir que o mesmo tipo de correção se
aplicaria a outros lugares, também vale a pena ver se esses lugares existem no
upstream -- se não existirem, é provável que o patch precise ser ajustado. O
``git log`` é seu amigo para descobrir o que aconteceu com essas áreas, já que o
``git blame`` não mostrará código que foi removido.

Se você encontrar outras instâncias do mesmo padrão na árvore do upstream e não
tiver certeza se isso também é um bug, pode valer a pena perguntar ao autor do
patch. Não é incomum encontrar novos bugs durante o processo de backport!

Verificando o resultado
=======================

colordiff
---------

Tendo commitado um novo patch sem conflitos, você pode agora comparar o seu
patch com o patch original. É altamente recomendável que você use uma
ferramenta como o `colordiff`_ que possa mostrar dois arquivos lado a lado e
colori-los de acordo com as alterações entre eles::

    colordiff -yw -W 200 <(git diff -W <upstream commit>^-) <(git diff -W HEAD^-) | less -SR

.. _colordiff: https://www.colordiff.org/

Aqui, ``-y`` significa fazer uma comparação lado a lado; ``-w`` ignora
espaços em branco e ``-W 200`` define a largura da saída (caso contrário, ele
usará 130 por padrão, o que costuma ser um pouco pouco).

A sintaxe ``rev^-`` é um atalho prático para ``rev^..rev``, essencialmente
fornecendo apenas o diff para aquele único commit; veja também a
`documentação oficial do git rev-parse`_.

.. _documentação oficial do git rev-parse: https://git-scm.com/docs/git-rev-parse#_other_rev_parent_shorthand_notations

Novamente, note a inclusão de ``-W`` para o ``git diff``; isso garante que você
verá a função completa para qualquer função que tenha mudado.

Uma coisa incrivelmente importante que o colordiff faz é destacar as linhas que
são diferentes. Por exemplo, se um ``goto`` de tratamento de erros teve seus
rótulos alterados entre o patch original e o que sofreu o backport, o colordiff
irá mostrá-los lado a lado, mas destacados em uma cor diferente. Assim, é fácil
ver que as duas instruções ``goto`` estão saltando para rótulos diferentes. Da
mesma forma, linhas que não foram modificadas por nenhum dos patches, mas que
diferem no contexto, também serão destacadas e, portanto, se destacarão durante
uma inspeção manual.

Claro, esta é apenas uma inspeção visual; o teste real é compilar e executar o
kernel (ou programa) com o patch aplicado.

Testes de compilação (Build testing)
------------------------------------

Não abordaremos os testes em tempo de execução aqui, mas pode ser uma boa ideia
compilar apenas os arquivos tocados pelo patch como uma verificação rápida de
sanidade. Para o kernel Linux, você pode compilar arquivos únicos assim,
assumindo que você tenha o ``.config`` e o ambiente de compilação configurados
corretamente::

    make caminho/para/o/arquivo.o

Note que isso não descobrirá erros de ligação (linker errors), então você ainda
deve fazer uma compilação completa após verificar que o arquivo único compila.
Ao compilar o arquivo único primeiro, você pode evitar ter que esperar por uma
compilação completa *caso* haja erros de compilador em qualquer um dos arquivos
que você alterou.

Testes em tempo de execução
---------------------------

Mesmo um teste de compilação ou de boot bem-sucedido não é necessariamente o
suficiente para descartar uma dependência ausente em algum lugar. Embora as
chances sejam pequenas, pode haver alterações de código onde duas modificações
independentes no mesmo arquivo resultem em nenhum conflito, nenhum erro em tempo
de compilação e erros em tempo de execução apenas em casos excepcionais.

Um exemplo concreto disso foi um par de patches para o código de entrada de
chamada de sistema (system call entry code), onde o primeiro patch salvava/
restaurava um registrador e um patch posterior fazia uso do mesmo registrador
em algum lugar no meio dessa sequência. Como não havia sobreposição entre as
alterações, era possível fazer o cherry-pick do segundo patch, não ter conflitos
e acreditar que tudo estava bem, quando na verdade o código estava agora
sobrescrevendo (scribbling over) um registrador não salvo.

Embora a vasta maioria dos erros seja capturada durante a compilação ou ao
exercitar o código superficialmente, a única maneira de *realmente* verificar um
backport é revisar o patch final com o mesmo nível de escrutínio que você daria
(ou deveria dar) a qualquer outro patch. Ter testes unitários e testes de
regressão ou outros tipos de testes automáticos pode ajudar a aumentar a
confiança na correção de um backport.

Enviando backports para a árvore estável (stable)
=================================================

À medida que os mantenedores da árvore estável tentam aplicar correções da linha
principal (mainline) em seus kernels estáveis via cherry-pick, eles podem enviar
e-mails solicitando backports quando encontram conflitos; veja, por exemplo,
<https://lore.kernel.org/stable/2023101528-jawed-shelving-071a@gregkh/>.
Esses e-mails normalmente incluem os passos exatos que você precisa seguir para
fazer o cherry-pick do patch para a árvore correta e enviá-lo.

Uma coisa a se certificar é que o seu registro de alterações (changelog) esteja
em conformidade com o formato esperado::

  <original patch title>

  [ Upstream commit <mainline rev> ]

  <rest of the original changelog>
  [ <summary of the conflicts and their resolutions> ]
  Signed-off-by: <your name and email>

A linha "Upstream commit" às vezes é ligeiramente diferente dependendo da versão
estável. Versões mais antigas usavam este formato::

  commit <mainline rev> upstream.

O mais comum é indicar a versão do kernel à qual o patch se aplica na linha de
assunto do e-mail (usando, por exemplo,
``git send-email --subject-prefix='PATCH 6.1.y'``), mas você também pode
colocá-la na área do Signed-off-by: ou abaixo da linha ``---``.

Os mantenedores da árvore estável esperam envios separados para cada versão
estável ativa, e cada envio também deve ser testado separadamente.

Algumas palavras finais de conselho
===================================

1) Aborde o processo de backport com humildade.
2) Entenda o patch para o qual você está fazendo o backport; isso significa ler
   tanto o registro de alterações (changelog) quanto o código.
3) Seja honesto sobre a sua confiança no resultado ao enviar o patch.
4) Peça aprovações explícitas (acks) aos mantenedores relevantes.

Exemplos
========

O texto acima mostra, de forma geral, o processo idealizado de backport de um
patch. Para um exemplo mais concreto, veja este tutorial em vídeo onde dois
patches são portados da linha principal (mainline) para a estável (stable):
`Backporting Linux Kernel Patches`_.

.. _Backporting Linux Kernel Patches: https://youtu.be/sBR7R1V2FeA